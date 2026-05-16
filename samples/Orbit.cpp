#include "Compositor.h"
#include "Renderer.h"
#include "PropKit.h"
#include "ECS.h"
#include "GltfLoader.h"
#include "ImGuiRenderer.h"
#include "Level.h"
#include "MouseNav.h"
#include "PerfMetrics.h"
#include "PhysicsSolver.h"
#include "Projection.h"
#include "Scene.h"
#include "scope_exit.h"
#include "Shapes.h"
#include "Stopwatch.h"
#include "ThreadPool.h"
#include "WebgpuHelper.h"

#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <random>
#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <thread>

static constexpr const char* APP_NAME = "Orbit";

static constexpr float PHYSICS_FPS = 60.0f;
static constexpr float PHYSICS_TIME_STEP = 1.0f/PHYSICS_FPS;
static constexpr float RENDER_FPS = 60.0f;
static constexpr float GRAVITATIONAL_CONSTANT = 0.1f;//6.674e-11f;//(m^3 kg^-1 s^-2)

namespace
{

class WorldMatrix : public Mat44f
{
public:
    using Mat44f::Mat44f;

    WorldMatrix& operator=(const Mat44& that)
    {
        this->Mat44f::operator=(that);
        return *this;
    }
};

// Used to tag entities that represent loaded models in the ECS registry.
struct ModelTag{};

}

static Result<>
Startup()
{
    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    MLG_CHECK(WebgpuHelper::Startup(APP_NAME));

    return Result<>::Ok;
}

static void
Shutdown()
{
    WebgpuHelper::Shutdown();
}

static Result<> RenderGui(const PhysicsSolver& solver);

static Result<>
Load(const std::filesystem::path& path,
    TextureCache& textureCache,
    PropKit& outPropKit,
    Level& outLevel)
{
    auto shape = Shapes::Ball(1.0f, 10);

    PropKitDef propKitDef //
        {
            .ModelDefs //
            {
                {
                    .Name{ "Shape" },
                    .MeshDefs //
                    {
                        {
                            .Vertices{ shape.GetVertices().begin(), shape.GetVertices().end() },
                            .Indices{ shape.GetIndices().begin(), shape.GetIndices().end() },
                        },
                    },
                },
            },
        };

    MLG_CHECK(PropKit::Create(path.parent_path(), textureCache, propKitDef, outPropKit),
        "Failed to create PropKit for {}",
        path.string());

    std::mt19937 gen(12345); // Fixed seed for reproducibility
    std::uniform_real_distribution<float> dis(-1, 1);

    constexpr int GRID_SIZE = 20;
    constexpr float MAX_RADIUS = 1.0f;
    constexpr float MIN_RADIUS = 0.1f;
    constexpr float MAX_SPEED = 0.5f;
    constexpr float MIN_SPEED = 0.1f;
#ifndef NDEBUG
    // Reduce the number of bodies in debug builds to improve performance.
    constexpr size_t NUM_BODIES = 500;
#else
    constexpr size_t NUM_BODIES = 1000;
#endif

    std::vector<LevelNodeDef> nodeDefs;
    nodeDefs.reserve(NUM_BODIES);
    for(size_t i = 0; i < nodeDefs.capacity(); ++i)
    {
        const float radius = MIN_RADIUS + std::abs(dis(gen)) * (MAX_RADIUS - MIN_RADIUS);
        const float mass = radius;
        const Vec3f position{ dis(gen) * GRID_SIZE, dis(gen) * GRID_SIZE, dis(gen) * GRID_SIZE };
        const Vec3f velocity = Vec3f{ dis(gen), dis(gen), dis(gen) }.Normalize() *
                               (MIN_SPEED + std::abs(dis(gen)) * (MAX_SPEED - MIN_SPEED));

        LevelNodeDef nodeDef//
        {
            .Name{ std::format("Body{}", i) },
            .Transform{ .T{position}, .S{ radius } },
            .Components //
            {
                .Model = ModelRef{ .Name = "Shape" },
                .Body = RigidBodyDef{ .LinearVelocity{ velocity }, .Mass{ mass } },
                .Collider = ColliderDef{ SphereDef{ .Radius = radius } },
            },
        };

        nodeDefs.emplace_back(std::move(nodeDef));
    }

    LevelDef levelDef //
    {
        .NodeDefs = std::move(nodeDefs),
    };

    MLG_CHECK(Level::Create(levelDef, outPropKit, outLevel),
        "Failed to create Level for {}",
        path.string());

    return Result<>::Ok;
}

static float s_TotalPotentialEnergy = 0.0f;

struct ApplyGravityBatchParams
{
    const size_t StartIndexA{0};
    const size_t StartIndexB{0};
    const size_t BatchSize{0};

    const std::span<const RigidBody> Bodies;
    const std::span<const TrsTransformf> Transforms;
    const std::span<const Collider> Colliders;

    std::vector<Vec3f> Forces;
    float PotentialEnergy{0};

    std::atomic<size_t>* FinishCounter{nullptr};
};

static void ApplyGravityBatch(ApplyGravityBatchParams* batchParams)
{
    batchParams->Forces.clear();
    batchParams->Forces.resize(batchParams->Bodies.size(), Vec3f{ 0 });
    batchParams->PotentialEnergy = 0;

    size_t count = 0;

    size_t j = batchParams->StartIndexB;

    for(size_t i = batchParams->StartIndexA;
        i < batchParams->Bodies.size() && count < batchParams->BatchSize;
        ++i, j = i + 1)
    {
        const float radiusA = batchParams->Colliders[i].GetSphereRadius();
        const Vec3f posA = batchParams->Transforms[i].T; // For cache friendliness this is not a reference.
        const float massA = batchParams->Bodies[i].Mass.Value();

        MLG_ASSERT(j < batchParams->Bodies.size(), "StartIndexB must be greater than StartIndexA");

        for(; j < batchParams->Bodies.size() && count < batchParams->BatchSize; ++j, ++count)
        {
            const float radiusB = batchParams->Colliders[j].GetSphereRadius();
            const float massB = batchParams->Bodies[j].Mass.Value();

            const float minSeparation = radiusA + radiusB;
            const float minSeparationSq = minSeparation * minSeparation;

            // Vector from body A to body B
            const Vec3f& posB = batchParams->Transforms[j].T;
            const Vec3f delta = posB - posA;

            // Gravitational force magnitude: F = G * (M * m) / r^2
            // Direction toward source: delta / r
            // Combined vector form: F = G * M * m * delta / r^3

            // If bodies overlap clamp to minimum separation.
            const float r2 = std::max(delta.Dot(delta), minSeparationSq);
            const float massProduct = massA * massB;

            const float pe = -GRAVITATIONAL_CONSTANT * massProduct / std::sqrtf(r2);
            const Vec3f F = -pe * delta / r2;
            //const Vec3f F = GRAVITATIONAL_CONSTANT * massProduct * delta / (r2 * std::sqrtf(r2));

            batchParams->PotentialEnergy += pe;

            batchParams->Forces[i] += F;
            batchParams->Forces[j] -= F;
        }
    }

    batchParams->FinishCounter->fetch_add(1, std::memory_order_relaxed);
}

#define APPLY_GRAVITY_MULTITHREADED 1

static void ApplyGravity(PhysicsSolver& solver)
{
    static PerfTimer perfTimer("Physics.ApplyGravity");
    auto scopedTimer = perfTimer.StartScoped();

    const std::span<const RigidBody> bodies = solver.GetBodies();
    const std::span<const TrsTransformf> transforms = solver.GetTransforms();
    const std::span<const Collider> colliders = solver.GetColliders();

    const size_t numPairs = bodies.size() * (bodies.size() - 1) / 2;
    const size_t workerCount = ThreadPool::GetWorkerCount();
    const size_t batchSize = (numPairs + workerCount - 1) / workerCount;
    const size_t numBatches = (numPairs + batchSize - 1) / batchSize;

    size_t pairCount = 0;

    std::atomic<size_t> finishCounter;
    std::vector<ApplyGravityBatchParams> batches;
    batches.reserve(numBatches);

    size_t startIndexA = 0, startIndexB = 1;

    for(size_t i = 0; i < bodies.size(); ++i)
    {
        for(size_t j = i + 1; j < bodies.size(); ++j, ++pairCount)
        {
            if(pairCount >= batchSize)
            {
                ApplyGravityBatchParams batchParams //
                    {
                        .StartIndexA = startIndexA,
                        .StartIndexB = startIndexB,
                        .BatchSize = pairCount,
                        .Bodies = bodies,
                        .Transforms = transforms,
                        .Colliders = colliders,
                        .FinishCounter = &finishCounter,
                    };

                ApplyGravityBatchParams& params = batches.emplace_back(batchParams);

#if APPLY_GRAVITY_MULTITHREADED
                ThreadPool::Enqueue<ApplyGravityBatch>(&params);
#else
                ApplyGravityBatch(&params);
#endif // APPLY_GRAVITY_MULTITHREADED

                pairCount = 0;
                startIndexA = i;
                startIndexB = j;
            }
        }
    }

    if(pairCount > 0)
    {
        // Process the last batch.
        ApplyGravityBatchParams batchParams //
            {
                .StartIndexA = startIndexA,
                .StartIndexB = startIndexB,
                .BatchSize = pairCount,
                .Bodies = bodies,
                .Transforms = transforms,
                .Colliders = colliders,
                .FinishCounter = &finishCounter,
            };

        ApplyGravityBatchParams& params = batches.emplace_back(batchParams);

#if APPLY_GRAVITY_MULTITHREADED
        ThreadPool::Enqueue<ApplyGravityBatch>(&params);
#else
        ApplyGravityBatch(&params);
#endif // APPLY_GRAVITY_MULTITHREADED
    }

#if APPLY_GRAVITY_MULTITHREADED
    while(finishCounter.load(std::memory_order_relaxed) < batches.size())
    {
        std::this_thread::yield();
    }
#endif // APPLY_GRAVITY_MULTITHREADED

    MLG_ASSERT(batches.size() == numBatches);

    s_TotalPotentialEnergy = 0;

    for(const auto& params : batches)
    {
        for(size_t i = 0; i < params.Forces.size(); ++i)
        {
            solver.AddForce(i, params.Forces[i]);
        }

        s_TotalPotentialEnergy += params.PotentialEnergy;
    }
}

static void ApplyExplosionImpulse(PhysicsSolver& solver, const float magnitude)
{
    const std::span<const RigidBody> bodies = solver.GetBodies();
    std::mt19937 gen;
    std::uniform_real_distribution<float> dis(0.5, 1);
    std::bernoulli_distribution sign;

    for(size_t i = 0; i < bodies.size(); ++i)
    {
        // Impulse is force integrated over time.
        // J = integral from t0 to t1 ​​F(t)dt
        // J = F * dt (if we assume the force is constant over the time step)
        // F = J / dt
        // F = mv / dt (to achieve a change in velocity of v over the time step)

        // Randomize the direction of the impulse.
        Vec3f normal //
            {
                dis(gen) * (sign(gen) ? 1.0f : -1.0f),
                dis(gen) * (sign(gen) ? 1.0f : -1.0f),
                dis(gen) * (sign(gen) ? 1.0f : -1.0f),
            };

        normal.Normalize();
        const Vec3f v = normal * magnitude;
        const float m = bodies[i].Mass.Value();

        const Vec3f force = m * v / PHYSICS_TIME_STEP;
        solver.AddForce(i, force);
    }
}

static void ApplyStoppingImpulse(PhysicsSolver& solver)
{
    const std::span<const RigidBody> bodies = solver.GetBodies();

    for(size_t i = 0; i < bodies.size(); ++i)
    {
        // Apply the impulse opposite to current velocity.
        const Vec3f impulse = -bodies[i].LinearVelocity * bodies[i].Mass.Value() / PHYSICS_TIME_STEP;
        solver.AddForce(i, impulse);
    }
}

static Result<>
MainLoop()
{
    bool running = true;
    bool minimized = false;

    Renderer renderer;
    Compositor compositor;
    ImGuiRenderer imGuiRenderer;
    TextureCache textureCache;
    PropKit propKit;
    Level level;
    Scene scene;
    PhysicsSolver solver;
    EcsRegistry registry;
    WalkMouseNav mouseNav;

    MLG_CHECK(renderer.Startup());
    MLG_CHECK(compositor.Startup());
    MLG_CHECK(imGuiRenderer.Startup());
    MLG_CHECK(textureCache.Startup());

    MLG_CHECK(Load("", textureCache, propKit, level));

    MLG_CHECK(Scene::Create(level, propKit, scene));

    MLG_CHECK(PhysicsSolver::Create(level, solver));

    Entity model = registry.CreateEntity(TrsTransformf{}, WorldMatrix{}, ModelTag{});

    Entity camera = registry.CreateEntity(TrsTransformf{.T{0,0,-40}}, WorldMatrix{}, Projection{});

    mouseNav.SetTransform(camera.Get<TrsTransformf>());

    uint64_t frameBeginTicks = SDL_GetTicksNS();

    bool mouseCaptured = true;
    SDL_SetWindowRelativeMouseMode(WebgpuHelper::GetWindow(), mouseCaptured);

    while(running)
    {
        PerfMetrics::BeginFrame();

        static PerfTimer frameTimer("Frame");
        frameTimer.Start();

        const uint64_t curTicksNs = SDL_GetTicksNS();
        const uint64_t elapsedTicksNs = curTicksNs - frameBeginTicks;
        const float elapsedSeconds = SDL_NS_TO_SECONDS(static_cast<float>(elapsedTicksNs));
        frameBeginTicks = curTicksNs;

        SDL_Event event;

        while(minimized && running && SDL_PollEvent(&event))
        {
            switch(event.type)
            {
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    minimized = false;
                    break;
            }
        }

        if(minimized)
        {
            std::this_thread::yield();
            continue;
        }

        while(!minimized && running && SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                //app->OnResize(event.window.data1, event.window.data2);
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                minimized = true;
                break;

            //case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                mouseNav.ClearButtons();
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                mouseNav.ClearButtons();
                break;

            case SDL_EVENT_MOUSE_MOTION:
                if(mouseCaptured)
                {
                    mouseNav.OnMouseMove(Vec2f(event.motion.xrel, event.motion.yrel));
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if(mouseCaptured)
                {
                    mouseNav.OnScroll(Vec2f(event.wheel.x, event.wheel.y));
                }
                break;

            case SDL_EVENT_KEY_DOWN:
                if(mouseCaptured)
                {
                    mouseNav.OnKeyDown(event.key.scancode);
                }

                if(SDL_SCANCODE_ESCAPE == event.key.scancode)
                {
                    running = false;
                }
                else if(SDL_SCANCODE_SPACE == event.key.scancode)
                {
                    mouseCaptured = !mouseCaptured;
                    SDL_SetWindowRelativeMouseMode(WebgpuHelper::GetWindow(), mouseCaptured);
                }
                else if(SDL_SCANCODE_RETURN == event.key.scancode)
                {
                    ApplyExplosionImpulse(solver, 5.0f);
                }
                else if(SDL_SCANCODE_BACKSPACE == event.key.scancode)
                {
                    ApplyStoppingImpulse(solver);
                }
                break;
            case SDL_EVENT_KEY_UP:
                if(mouseCaptured)
                {
                    mouseNav.OnKeyUp(event.key.scancode);
                }
                break;
            }
        }

        ApplyGravity(solver);

        solver.Update(PHYSICS_TIME_STEP);

        solver.SyncToLevel(level);

        scene.SyncFromLevel(level);

        mouseNav.Update(elapsedSeconds);

        auto screenBounds = WebgpuHelper::GetScreenBounds();
        const float aspectRatio = screenBounds.Width / screenBounds.Height;
        camera.Get<Projection>().SetAspectRatio(aspectRatio);
        camera.Get<TrsTransformf>() = mouseNav.GetTransform();

        // Transform roots
        for(const auto& tuple : registry.GetView<TrsTransformf, WorldMatrix>())
        {
            auto [eid, xform, worldMat] = tuple;
            worldMat = xform.ToMatrix();
        }

        compositor.BeginFrame();
        imGuiRenderer.NewFrame();

        scene.SyncToGpu();

        const auto& camWorldMat = camera.Get<WorldMatrix>();
        const auto& projection = camera.Get<Projection>();
        for(const auto& tuple : registry.GetView<WorldMatrix, ModelTag>())
        {
            const auto [eid, worldMat, modelTag] = tuple;

            renderer.Render(camWorldMat, projection, scene, propKit, compositor);
        }

        RenderGui(solver);

        imGuiRenderer.Render(compositor);

        compositor.EndFrame();

#if !defined(__EMSCRIPTEN__)
        MLG_CHECK(WebgpuHelper::GetSurface().Present(), "Failed to present backbuffer");
#endif

        WebgpuHelper::GetInstance().ProcessEvents();

        frameTimer.Stop();

        PerfMetrics::EndFrame();
    }

    MLG_CHECK(textureCache.Shutdown());
    MLG_CHECK(imGuiRenderer.Shutdown());
    MLG_CHECK(compositor.Shutdown());
    MLG_CHECK(renderer.Shutdown());

    PerfMetrics::LogTimers();

    return Result<>::Ok;
}

static Result<> RenderGui(const PhysicsSolver& solver)
{
    const char* buildType;
#if defined (NDEBUG)
    buildType = "Release";
#else
    buildType = "Debug";
#endif

    constexpr const char* backend = "Dawn";

    auto title = std::format("Timers: {}/{}", buildType, backend);

    ImGui::SetNextWindowSize(ImVec2(0, 0)); // Auto-fit both width and height
    ImGui::Begin(title.c_str());
    PerfMetrics::TimerStat timers[256];
    unsigned timerCount = PerfMetrics::GetTimers(timers, std::size(timers));
    for(unsigned i = 0; i < timerCount; ++i)
    {
        ImGui::Text("%s: %.3f ms", timers[i].GetName().c_str(), timers[i].GetValue() * 1000.0f);
    }

    const float kineticEnergy = solver.ComputeKineticEnergy();
    ImGui::Separator();
    ImGui::Text("Kinetic Energy: %.3f", kineticEnergy);
    ImGui::Text("Potential Energy: %.3f", s_TotalPotentialEnergy);
    ImGui::Text("Total Energy: %.3f", kineticEnergy + s_TotalPotentialEnergy);

    ImGui::End();

    return Result<>::Ok;
}

int main(int /*argc*/, char** /*argv*/)
{
    if(!Startup())
    {
        return -1;
    }

    auto shutdown = scope_exit([]()
    {
        Shutdown();
    });

    if(!MainLoop())
    {
        return -1;
    }

    return 0;
}