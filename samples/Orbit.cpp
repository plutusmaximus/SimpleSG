#include "Camera.h"
#include "CliUi.h"
#include "Compositor.h"
#include "DevUi.h"
#include "FileFetcher.h"
#include "ImGuiRenderer.h"
#include "Level.h"
#include "MouseNav.h"
#include "PerfMetrics.h"
#include "PhysicsLevel.h"
#include "PropKit.h"
#include "Renderer.h"
#include "Scene.h"
#include "scope_exit.h"
#include "Shapes.h"
#include "TextureCache.h"
#include "ThreadPool.h"
#include "WebgpuHelper.h"

#include <filesystem>
#include <imgui_impl_sdl3.h>
#include <random>
#include <ranges>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <thread>

namespace
{
constexpr const char* APP_NAME = "Orbit";

constexpr float kPhysicsFps = 60.0f;
constexpr float kPhysicsTimeStep = 1.0f/kPhysicsFps;
constexpr float kGravitationalConstant = 0.1f;//6.674e-11f;//(m^3 kg^-1 s^-2)

constexpr bool kApplyGravityMultithreaded = true;
struct PerfCounterGlobals
{
    static inline PerfCounter TotalPE{"Energy.PE"};    // Potential Energy
    static inline PerfCounter TotalKE{"Energy.KE"};    // Kinetic Energy
    static inline PerfCounter TotalEnergy{"Energy.Total"};
};

Result<>
Startup()
{
    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    MLG_CHECK(ThreadPool::Startup());
    MLG_DEFER_AS(failure)
    {
        ThreadPool::Shutdown();
    };

    MLG_CHECK(FileFetcher::Startup());
    MLG_DEFER_AS(fileFetcherShutdown)
    {
        FileFetcher::Shutdown();
    };

    MLG_CHECK(WebgpuHelper::Startup(APP_NAME));

    failure.release();
    fileFetcherShutdown.release();

    return Result<>::Ok;
}

void
Shutdown()
{
    WebgpuHelper::Shutdown();
    FileFetcher::Shutdown();
    ThreadPool::Shutdown();
}

Result<std::tuple<PropKit, Level>>
Load(TextureCache& textureCache)
{
    constexpr float ballRadius = 1.0f;

    auto shape = Shapes::Ball(ballRadius);

    const PropKitDef propKitDef //
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

    auto propKit = PropKit::Create(std::filesystem::path{}, textureCache, propKitDef);
    MLG_CHECK(propKit, "Failed to create PropKit");

    // Fixed seed for reproducibility
    constexpr unsigned kRngSeed = 12345;
    std::mt19937 gen(kRngSeed);
    std::uniform_real_distribution<float> dis(-1, 1);

    constexpr int GRID_SIZE = 20;
    constexpr float MAX_RADIUS = 1.0f;
    constexpr float MIN_RADIUS = 0.1f;
#ifndef NDEBUG
    // Reduce the number of bodies in debug builds to improve performance.
    constexpr size_t NUM_BODIES = 500;
#else
    constexpr size_t NUM_BODIES = 1000;
#endif

    std::vector<LevelNodeDef> nodeDefs;
    nodeDefs.reserve(NUM_BODIES);
    for(size_t i  = 0; i < NUM_BODIES; ++i)
    {
        const float radius = MIN_RADIUS + (std::abs(dis(gen)) * (MAX_RADIUS - MIN_RADIUS));
        const float mass = radius;
        const Vec3f position{ dis(gen) * GRID_SIZE, dis(gen) * GRID_SIZE, dis(gen) * GRID_SIZE };

        LevelNodeDef nodeDef//
        {
            .Name{ std::format("Body{}", i) },
            .Transform{ .T{position}, .S{ radius } },
            .Components //
            {
                .Model = ModelRef{ .Name = "Shape" },
                .Body = RigidBodyDef{ .Mass{ mass } },
                .Collider = ColliderDef{ SphereDef{ .Center = Vec3f(0), .Radius = radius } },
            },
        };

        nodeDefs.emplace_back(std::move(nodeDef));
    }

    const LevelDef levelDef //
    {
        .NodeDefs = std::move(nodeDefs),
    };

    auto level = Level::Create(levelDef, *propKit);
    MLG_CHECK(level, "Failed to create Level");

    return std::make_tuple(std::move(*propKit), std::move(*level));
}

void ApplyRandomVelocities(PhysicsLevel& physLevel)
{
    constexpr float MAX_SPEED = 0.5f;
    constexpr float MIN_SPEED = 0.1f;
    constexpr unsigned kRngSeed = 12345;

    std::mt19937 gen(kRngSeed);
    std::uniform_real_distribution<float> dis(-1, 1);

    for(auto& vel : physLevel.GetLinearVelocities())
    {
        const Vec3f randomVel = Vec3f{ dis(gen), dis(gen), dis(gen) }.Normalize() *
            (MIN_SPEED + (std::abs(dis(gen)) * (MAX_SPEED - MIN_SPEED)));

        vel = randomVel;
    }
}

struct ApplyGravityBatchParams
{
    size_t StartIndexA{0};
    size_t StartIndexB{0};
    size_t BatchSize{0};

    std::span<const RigidBody> Bodies;
    std::span<const TrsTransformf> Transforms;
    std::span<const Collider> Colliders;

    std::vector<Vec3f> Forces;
    float PotentialEnergy{0};

    std::atomic<size_t>* FinishCounter{nullptr};
};

void ApplyGravityBatch(ApplyGravityBatchParams* batchParams)
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
        const BoundingSphere& sphereA = batchParams->Colliders[i].GetEnclosingSphere();
        const float radiusA = sphereA.GetRadius();
        const Vec3f posA = batchParams->Transforms[i].T + sphereA.GetCenter();
        const float massA = batchParams->Bodies[i].GetMass().Value();

        MLG_ASSERT(j < batchParams->Bodies.size(), "StartIndexB must be greater than StartIndexA");

        for(; j < batchParams->Bodies.size() && count < batchParams->BatchSize; ++j, ++count)
        {
            const BoundingSphere& sphereB = batchParams->Colliders[j].GetEnclosingSphere();
            const float radiusB = sphereB.GetRadius();
            const float massB = batchParams->Bodies[j].GetMass().Value();

            const float minSeparation = radiusA + radiusB;
            const float minSeparationSq = minSeparation * minSeparation;

            // Vector from body A to body B
            const Vec3f posB = batchParams->Transforms[j].T + sphereB.GetCenter();
            const Vec3f delta = posB - posA;

            // Gravitational force magnitude: F = G * (M * m) / r^2
            // Direction toward source: delta / r
            // Combined vector form: F = G * M * m * delta / r^3

            // If bodies overlap clamp to minimum separation.
            const float r2 = std::max(delta.Dot(delta), minSeparationSq);
            const float massProduct = massA * massB;

            const float pe = -kGravitationalConstant * massProduct / std::sqrt(r2);
            const Vec3f F = -pe * delta / r2;
            //const Vec3f F = kGravitationalConstant * massProduct * delta / (r2 * std::sqrt(r2));

            batchParams->PotentialEnergy += pe;

            batchParams->Forces[i] += F;
            batchParams->Forces[j] -= F;
        }
    }

    batchParams->FinishCounter->fetch_add(1, std::memory_order_relaxed);
}

// Returns the total potential energy of the system after applying gravity.
void ApplyGravity(PhysicsLevel& physLevel)
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
                const ApplyGravityBatchParams batchParams //
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

                if constexpr (kApplyGravityMultithreaded)
                {
                    ThreadPool::Enqueue<ApplyGravityBatch>(&params);
                }
                else
                {
                    ApplyGravityBatch(&params);
                }

                pairCount = 0;
                startIndexA = i;
                startIndexB = j;
            }
        }
    }

    if(pairCount > 0)
    {
        // Process the last batch.
        const ApplyGravityBatchParams batchParams //
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

        if constexpr (kApplyGravityMultithreaded)
        {
            ThreadPool::Enqueue<ApplyGravityBatch>(&params);
        }
        else
        {
            ApplyGravityBatch(&params);
        }
    }

    if constexpr (kApplyGravityMultithreaded)
    {
        while(finishCounter.load(std::memory_order_relaxed) < batches.size())
        {
            std::this_thread::yield();
        }
    }

    MLG_ASSERT(batches.size() == numBatches);

    float totalPotentialEnergy = 0;

    for(const auto& params : batches)
    {
        for(size_t i = 0; i < params.Forces.size(); ++i)
        {
            physLevel.AddForce(i, params.Forces[i]);
        }

        totalPotentialEnergy += params.PotentialEnergy;
    }

    PerfCounterGlobals::TotalPE.Set(totalPotentialEnergy);
}

void ApplyExplosionImpulse(PhysicsLevel& physLevel, const float magnitude)
{
    const std::span<const RigidBody> bodies = physLevel.GetBodies();
    constexpr unsigned kRngSeed = 12345;
    std::mt19937 gen(kRngSeed);
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

        normal = normal.Normalize();
        const Vec3f v = normal * magnitude;
        const float m = bodies[i].GetMass().Value();

        const Vec3f force = m * v / kPhysicsTimeStep;
        physLevel.AddForce(i, force);
    }
}

void ApplyStoppingImpulse(PhysicsLevel& physLevel)
{
    const std::span<const Vec3f> velocities = physLevel.GetLinearVelocities();
    const std::span<const RigidBody> bodies = physLevel.GetBodies();

    for(size_t i = 0; i < bodies.size(); ++i)
    {
        // Apply the impulse opposite to current velocity.
        const Vec3f impulse = -velocities[i] * bodies[i].GetMass().Value() / kPhysicsTimeStep;
        physLevel.AddForce(i, impulse);
    }
}

[[maybe_unused]] void
DeactivateNonOverlappingBodies(const PhysicsLevel& physLevel, Level& level)
{
    const std::span<const Level::NodeHandle> nodeHandles = physLevel.GetNodeHandles();
    const std::span<const RigidBody> bodies = physLevel.GetBodies();
    const std::span<const TrsTransformf> transforms = physLevel.GetTransforms();
    const std::span<const Collider> colliders = physLevel.GetColliders();

    // First deactivate all bodies.
    // Then activate only bodies that are overlapping with another body.
    for(const auto& nodeHandle : nodeHandles)
    {
        level.SetActive(nodeHandle, false);
        level.SetVisible(nodeHandle, false);
    }

    for(size_t i = 0; i < bodies.size(); ++i)
    {
        const BoundingSphere& sphereA = colliders[i].GetEnclosingSphere();
        const float radiusA = sphereA.GetRadius();
        const Vec3f& posA = transforms[i].T + sphereA.GetCenter();

        for(size_t j = i + 1; j < bodies.size(); ++j)
        {
            const BoundingSphere& sphereB = colliders[j].GetEnclosingSphere();
            const float radiusB = sphereB.GetRadius();
            const Vec3f& posB = transforms[j].T + sphereB.GetCenter();

            const float minSeparation = radiusA + radiusB;
            const float minSeparationSq = minSeparation * minSeparation;
            const Vec3f dPos = posB - posA;
            const float dPosSq = dPos.Dot(dPos);

            if(dPosSq < minSeparationSq)
            {
                level.SetActive(nodeHandles[i], true);
                level.SetVisible(nodeHandles[i], true);

                level.SetActive(nodeHandles[j], true);
                level.SetVisible(nodeHandles[j], true);
            }
        }
    }
}

[[maybe_unused]] void ActivateAllBodies(PhysicsLevel& physLevel, Level& level)
{
    const std::span<const Level::NodeHandle> nodeHandles = physLevel.GetNodeHandles();

    for(const auto& nodeHandle : nodeHandles)
    {
        level.SetActive(nodeHandle, true);
        level.SetVisible(nodeHandle, true);
    }
}

void ComputeKineticEnergy(const PhysicsLevel& physLevel)
{
    float kineticEnergy = 0.0f;

    const std::span<const RigidBody> bodies = physLevel.GetBodies();
    const std::span<const Vec3f> velocities = physLevel.GetLinearVelocities();

    auto range = std::views::zip(bodies, velocities);

    for(const auto& [body, velocity] : range)
    {
        const float mass = body.GetMass().Value();
        const float speedSq = velocity.Dot(velocity);
        // KE = mv^2 / 2
        kineticEnergy += 0.5f * mass * speedSq;
    }

    PerfCounterGlobals::TotalKE.Set(kineticEnergy);

    PerfCounterGlobals::TotalEnergy.Set(kineticEnergy + PerfCounterGlobals::TotalPE.GetValue());
}

Result<>
MainLoop()
{
    bool running = true;
    bool minimized = false;

    Renderer renderer;
    Compositor compositor;
    ImGuiRenderer imGuiRenderer;
    TextureCache textureCache;
    PhysicsLevel physLevel;
    WalkMouseNav mouseNav;
    CliUi cliUi;
    DevUi devUi(cliUi, renderer);

    MLG_CHECK(renderer.Startup());
    MLG_CHECK(imGuiRenderer.Startup());
    MLG_CHECK(textureCache.Startup());

    auto loadResult = Load(textureCache);
    MLG_CHECK(loadResult);

    auto&& [propKit, level] = std::move(*loadResult);

    auto sceneResult = Scene::Create(level, propKit);
    MLG_CHECK(sceneResult);

    Scene scene = std::move(*sceneResult);

    MLG_CHECK(PhysicsLevel::Create(level, physLevel));
    ApplyRandomVelocities(physLevel);

    constexpr float kInitialCameraDistance = 40.0f;

    Posef cameraXForm{ .T{0, 0, -kInitialCameraDistance} };
    Camera camera((Viewport(WebgpuHelper::GetScreenBounds())));

    mouseNav.SetTransform(cameraXForm);

    bool mouseCaptured = false;

    Timer frameTimer;

    bool pauseSim = false;
    bool showOverlappingBodies = true;
    bool continuouslyDeactivateNonOverlappingBodies = false;

    while(running)
    {
        MLG_SCOPED_TIMER(" Frame");

        const float elapsedSeconds = frameTimer.GetElapsedSeconds();

        frameTimer.Restart();

        SDL_Event event;

        while(minimized && running && SDL_PollEvent(&event))
        {
            ImGui_ImplSDL3_ProcessEvent(&event);

            switch(event.type)
            {
                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    minimized = false;
                    break;

                default:
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
            ImGui_ImplSDL3_ProcessEvent(&event);

            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            //case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                {
                    const uint32_t newWidth = static_cast<uint32_t>(event.window.data1);
                    const uint32_t newHeight = static_cast<uint32_t>(event.window.data2);
                    WebgpuHelper::Resize(newWidth, newHeight);
                }
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                minimized = true;
                break;

            //case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
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
                if(event.button.button == SDL_BUTTON_LEFT)
                {
                    mouseCaptured = true;
                    SDL_SetWindowRelativeMouseMode(WebgpuHelper::GetWindow(), mouseCaptured);
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if(event.button.button == SDL_BUTTON_LEFT)
                {
                    mouseCaptured = false;
                    SDL_SetWindowRelativeMouseMode(WebgpuHelper::GetWindow(), mouseCaptured);
                    mouseNav.ClearButtons();
                }
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
                else if(SDL_SCANCODE_RETURN == event.key.scancode)
                {
                    constexpr float kImpulseMagnitude = 5.0f;
                    ApplyExplosionImpulse(physLevel, kImpulseMagnitude);
                }
                else if(SDL_SCANCODE_BACKSPACE == event.key.scancode)
                {
                    ApplyStoppingImpulse(physLevel);
                }
                else if(SDL_SCANCODE_F1 == event.key.scancode)
                {
                    pauseSim = !pauseSim;
                }
                else if(SDL_SCANCODE_F2 == event.key.scancode)
                {
                    showOverlappingBodies = !showOverlappingBodies;
                    continuouslyDeactivateNonOverlappingBodies = false;

                    if(showOverlappingBodies)
                    {
                        ActivateAllBodies(physLevel, level);
                    }
                    else
                    {
                        DeactivateNonOverlappingBodies(physLevel, level);
                    }
                }
                else if(SDL_SCANCODE_F3 == event.key.scancode)
                {
                    continuouslyDeactivateNonOverlappingBodies = true;
                }
                break;
            case SDL_EVENT_KEY_UP:
                if(mouseCaptured)
                {
                    mouseNav.OnKeyUp(event.key.scancode);
                }
                break;

            default:
                break;
            }
        }

        if(continuouslyDeactivateNonOverlappingBodies)
        {
            DeactivateNonOverlappingBodies(physLevel, level);
        }

        if(!pauseSim)
        {
            ApplyGravity(physLevel);

            physLevel.Update(kPhysicsTimeStep);

            ComputeKineticEnergy(physLevel);
        }

        MLG_CHECK(physLevel.SyncToLevel(level));

        MLG_CHECK(scene.SyncFromLevel(level));

        mouseNav.Update(elapsedSeconds);
            
        const Viewport viewport(WebgpuHelper::GetScreenBounds());
        camera.SetViewport(viewport);
        const Rect& scenePanelRect = devUi.GetScenePanelRect();
        if(scenePanelRect.Width > 0 && scenePanelRect.Height > 0)
        {
            camera.SetAspectRatio(scenePanelRect.GetAspectRatio());
        }
        cameraXForm = mouseNav.GetTransform();

        MLG_CHECK(scene.SyncToGpu());

        MLG_CHECK(compositor.BeginFrame());

        MLG_CHECK(renderer.Render(camera, cameraXForm, scene, propKit));
        //MLG_CHECK(renderer.Composite(compositor));

        MLG_CHECK(imGuiRenderer.NewFrame());
        MLG_CHECK(devUi.Render());
        MLG_CHECK(imGuiRenderer.Composite(compositor));

        MLG_CHECK(compositor.EndFrame());

        {
#if !defined(__EMSCRIPTEN__)
            MLG_SCOPED_TIMER("Present");
            MLG_CHECK(WebgpuHelper::GetSurface().Present(), "Failed to present backbuffer");
#endif
        }

        WebgpuHelper::GetInstance().ProcessEvents();
    }

    MLG_CHECK(textureCache.Shutdown());
    MLG_CHECK(imGuiRenderer.Shutdown());
    MLG_CHECK(renderer.Shutdown());

    PerfMetrics::LogCounters();

    return Result<>::Ok;
}
} // namespace

int main(int /*argc*/, char** /*argv*/)
{
    if(!Startup())
    {
        return -1;
    }

    MLG_DEFER
    {
        Shutdown();
    };

    if(!MainLoop())
    {
        return -1;
    }

    return 0;
}