#include "Compositor.h"
#include "Renderer.h"
#include "PropKit.h"
#include "GltfLoader.h"
#include "ImGuiRenderer.h"
#include "Level.h"
#include "MouseNav.h"
#include "PerfMetrics.h"
#include "PhysicsLevel.h"
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
static constexpr float GRAVITATIONAL_CONSTANT = 0.1f;//6.674e-11f;//(m^3 kg^-1 s^-2)

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

static Result<> RenderGui(const PhysicsLevel& physLevel);

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

    const float radius = 1;
    const float mass = radius;
    // Half overlap
    //const Vec3f positionA{ 0.5f, 0, 0 };
    //const Vec3f positionB{ -0.5f, 0, 0 };

    // Full overlap
    //const Vec3f positionA{ 0, 0, 0 };
    //const Vec3f positionB{ 0, 0, 0 };

    // Barely overlap
    //const Vec3f positionA{ 1 - 1e-6f, 0, 0 };
    //const Vec3f positionB{ -1 + 1e-6f, 0, 0 };

    // Touching
    const Vec3f positionA{ 1, 0, 0 };
    const Vec3f positionB{ -1, 0, 0 };

    //Glancing blow
    //const Vec3f velocityA = Vec3f{ -0.1f, 0.1f, 0 };
    //const Vec3f velocityB = Vec3f{ 0.1f, 0.1f, 0 };

    //Head on collision
    //const Vec3f velocityA = Vec3f{ -1, 0, 0 };
    //const Vec3f velocityB = Vec3f{ 1, 0, 0 };

    // Opposite directions
    //const Vec3f velocityA = Vec3f{ 1, 0, 0 };
    //const Vec3f velocityB = Vec3f{ -1, 0, 0 };

    // Zero velocity
    const Vec3f velocityA{ 0, 0, 0 };
    const Vec3f velocityB{ 0, 0, 0 };

    std::vector<LevelNodeDef> nodeDefs//
    {
        {
            .Name{ std::format("Body{}", "A") },
            .Transform{ .T{positionA}, .S{ radius } },
            .Components //
            {
                .Model = ModelRef{ .Name = "Shape" },
                .Body = RigidBodyDef{ .LinearVelocity{ velocityA }, .Mass{ mass } },
                .Collider = ColliderDef{ SphereDef{ .Radius = radius } },
            },
        },
        {
            .Name{ std::format("Body{}", "B") },
            .Transform{ .T{positionB}, .S{ radius } },
            .Components //
            {
                .Model = ModelRef{ .Name = "Shape" },
                .Body = RigidBodyDef{ .LinearVelocity{ velocityB }, .Mass{ mass } },
                .Collider = ColliderDef{ SphereDef{ .Radius = radius } },
            },
        }
    };

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

static void ApplyGravity(PhysicsLevel& physLevel)
{
    MLG_SCOPED_TIMER("Physics.ApplyGravity");

    const std::span<const RigidBody> bodies = physLevel.GetBodies();
    const std::span<const TrsTransformf> transforms = physLevel.GetTransforms();
    const std::span<const Collider> colliders = physLevel.GetColliders();

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
            physLevel.AddForce(i, params.Forces[i]);
        }

        s_TotalPotentialEnergy += params.PotentialEnergy;
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
    PhysicsLevel physLevel;
    WalkMouseNav mouseNav;

    MLG_CHECK(renderer.Startup());
    MLG_CHECK(imGuiRenderer.Startup());
    MLG_CHECK(textureCache.Startup());

    MLG_CHECK(Load("", textureCache, propKit, level));

    MLG_CHECK(Scene::Create(level, propKit, scene));

    MLG_CHECK(PhysicsLevel::Create(level, physLevel));

    TrsTransformf trsCamera{ .T{0, 0, -40} };
    Projection projection;

    mouseNav.SetTransform(trsCamera);

    uint64_t frameBeginTicks = SDL_GetTicksNS();

    bool mouseCaptured = true;
    SDL_SetWindowRelativeMouseMode(WebgpuHelper::GetWindow(), mouseCaptured);

    bool pauseSim = true;

    while(running)
    {
        MLG_SCOPED_TIMER("Frame");

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
                else if(SDL_SCANCODE_F1 == event.key.scancode)
                {
                    pauseSim = !pauseSim;
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

        if(!pauseSim)
        {
            ApplyGravity(physLevel);

            physLevel.Update(PHYSICS_TIME_STEP);
        }

        physLevel.SyncToLevel(level);

        scene.SyncFromLevel(level);

        mouseNav.Update(elapsedSeconds);

        auto screenBounds = WebgpuHelper::GetScreenBounds();
        const float aspectRatio = screenBounds.Width / screenBounds.Height;
        projection.SetAspectRatio(aspectRatio);
        trsCamera = mouseNav.GetTransform();

        compositor.BeginFrame();
        imGuiRenderer.NewFrame();

        scene.SyncToGpu();

        renderer.Render(trsCamera, projection, scene, propKit, compositor);

        RenderGui(physLevel);

        imGuiRenderer.Render(compositor);

        compositor.EndFrame();

#if !defined(__EMSCRIPTEN__)
        MLG_CHECK(WebgpuHelper::GetSurface().Present(), "Failed to present backbuffer");
#endif

        WebgpuHelper::GetInstance().ProcessEvents();
    }

    MLG_CHECK(textureCache.Shutdown());
    MLG_CHECK(imGuiRenderer.Shutdown());
    MLG_CHECK(renderer.Shutdown());

    PerfMetrics::LogTimers();

    return Result<>::Ok;
}

static Result<> RenderGui(const PhysicsLevel& physLevel)
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

    PerfTimerStats timerStats[256];
    unsigned timerCount = PerfMetrics::SampleTimers(timerStats, std::size(timerStats));
    for(unsigned i = 0; i < timerCount; ++i)
    {
        const std::string text =
            std::format("{}: {:.3f} ms", timerStats[i].GetName(), timerStats[i].GetEMA() * 1000.0f);
        ImGui::Text("%s", text.c_str());
    }

    const float kineticEnergy = physLevel.ComputeKineticEnergy();
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