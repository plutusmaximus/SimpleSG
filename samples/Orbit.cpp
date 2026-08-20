#include "CameraActor.h"
#include "CommonActionIds.h"
#include "DevUi.h"
#include "GpuHelper.h"
#include "Level.h"
#include "LuaRuntime.h"
#include "PerfMetrics.h"
#include "PropKit.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShapeMeshDefs.h"
#include "System.h"
#include "ThreadPool.h"

#include <filesystem>
#include <arm_neon.h>
#include <imgui.h>
#include <numbers>
#include <random>
#include <ranges>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <thread>
#include <vector>

// TODO
// gravity-only energy drift
// energy removed by restitution
// energy removed by resting contacts
// energy introduced by positional correction
// total numerical error

namespace
{
constexpr const char* kAppName = "Orbit";

constexpr float kPhysicsFps = 120.0f;
constexpr float kPhysicsTimeStep = 1.0f / kPhysicsFps;
constexpr float kGravitationalConstant = 0.1f; // 6.674e-11f;//(m^3 kg^-1 s^-2)

constexpr bool kApplyGravityMultithreaded = true;

struct PerfCounterGlobals
{
    static inline PerfCounter TotalPE{ { .Name = "Energy.PE" } }; // Potential Energy
    static inline PerfCounter TotalKE{ { .Name = "Energy.KE" } }; // Kinetic Energy
    static inline PerfCounter TotalEnergy{ { .Name = "Energy.Total" } };
};

Result<std::tuple<PropKit, Level>>
LoadLevel(GpuHelper& gpuHelper, ThreadPool& threadPool, FileFetcher& fileFetcher)
{
    constexpr float kBallRadius = 1.0f;
    //constexpr float kBoxExtent = kBallRadius * 2;

    // Fixed seed for reproducibility
    constexpr unsigned kRngSeed = 12345;
    std::mt19937 gen(kRngSeed);
    std::uniform_real_distribution<float> dis(-1, 1);

    constexpr float kDispersionRadius = 20;
    constexpr float kMaxBodyRadius = 1.0f;
    constexpr float kMinBodyRadius = 0.1f;

#ifndef NDEBUG
    // Reduce the number of bodies in debug builds to improve performance.
    constexpr size_t kNumBodies = 500;
#else
    constexpr size_t kNumBodies = 1000;
#endif

    const PropKitDef propKitDef //
        {
            .ModelDefs //
            {
                {
                    .Name{ "Shape" },
                    .MeshDefs //
                    {
                        ShapeMeshDefs::Ball({ .Radius = kBallRadius }),
                        //ShapeMeshDefs::Box({ .Width = kBoxExtent, .Height = kBoxExtent, .Depth = kBoxExtent }),
                    },
                },
            },
        };

    std::vector<RootNodeDef> nodeDefs;
    nodeDefs.reserve(kNumBodies);
    for(size_t i = 0; i < kNumBodies; ++i)
    {
        const float radius = kMinBodyRadius + (std::abs(dis(gen)) * (kMaxBodyRadius - kMinBodyRadius));
        const float mass = radius;
        const Vec3f position //
            {
                dis(gen) * kDispersionRadius,
                dis(gen) * kDispersionRadius,
                dis(gen) * kDispersionRadius,
            };

        RootNodeDef nodeDef //
            {
                .Name{ std::format("Body{}", i) },
                .Transform{ .T{ position }, .S{ radius } },
                .Model = ModelRef{ .Name = "Shape" },
                .Body =
                    RigidBodyDef //
                {
                    .Mass{ mass },
                    .MotionType = MotionType::Dynamic,
                    .Colliders =
                    {
                        ColliderDef //
                        {
                            .BoundingVolume =
                                SphereDef //
                            {
                                .Center = Vec3f(0),
                                .Radius = radius,
                            },
                            /*BoxDef //
                            {
                                .Center = Vec3f(0),
                                .HalfExtents = Vec3f(radius),
                            },*/
                            .CollisionType = CollisionType::Block,
                        },
                    },
                },
            };

        nodeDefs.push_back(std::move(nodeDef));
    }

    const LevelDef levelDef //
        {
            .NodeDefs = std::move(nodeDefs),
        };

    auto propKit =
        PropKit::Create(gpuHelper, threadPool, fileFetcher, std::filesystem::path{}, propKitDef);
    MLG_CHECK(propKit, "Failed to create PropKit");

    auto level = Level::Create(levelDef, *propKit);
    MLG_CHECK(level, "Failed to create Level");

    return std::make_tuple(std::move(*propKit), std::move(*level));
}

/// @brief Applies random linear velocities to all bodies in the physics level.
void
ApplyRandomVelocities(Level& level)
{
    constexpr float kMaxSpeed = 0.5f;//2.0f;
    constexpr float kMinSpeed = 0.1f;//1.0f;
    constexpr unsigned kRngSeed = 12345;

    std::mt19937 gen(kRngSeed);
    std::uniform_real_distribution<float> dis(-1, 1);

    for(PhysicsNode& node : level.GetAllPhysicsNodes())
    {
        const Vec3f randomNormal = Vec3f{ dis(gen), dis(gen), dis(gen) }.Normalize();
        const Vec3f randomVel =
            randomNormal * (kMinSpeed + (std::abs(dis(gen)) * (kMaxSpeed - kMinSpeed)));

        node.SetLinearVelocity(randomVel);
    }
}

struct ApplyGravityBatchParams
{
    size_t StartIndexA{ 0 };
    size_t StartIndexB{ 0 };
    size_t BatchSize{ 0 };

    std::span<const float> BodyX;
    std::span<const float> BodyY;
    std::span<const float> BodyZ;
    std::span<const float> InvMasses;

    std::span<float> ForceX;
    std::span<float> ForceY;
    std::span<float> ForceZ;
    
    double PotentialEnergyy{ 0 };

    std::atomic<size_t>* FinishCounter{ nullptr };
};

void
ApplyGravityRow(ApplyGravityBatchParams* batchParams, const size_t i, const size_t jStart, const size_t jEnd)
{
    constexpr float kMinDistance = 0.1f; // Minimum distance to avoid singularities in gravitational force calculations.
    constexpr float kMinDistance2 = kMinDistance * kMinDistance;

    const float ax = batchParams->BodyX[i];
    const float ay = batchParams->BodyY[i];
    const float az = batchParams->BodyZ[i];
    const float am = 1.0f / batchParams->InvMasses[i];

    const float* __restrict centerx = batchParams->BodyX.data();
    const float* __restrict centery = batchParams->BodyY.data();
    const float* __restrict centerz = batchParams->BodyZ.data();
    const float* __restrict invMasses = batchParams->InvMasses.data();

    float* __restrict fx = batchParams->ForceX.data();
    float* __restrict fy = batchParams->ForceY.data();
    float* __restrict fz = batchParams->ForceZ.data();

    float forceAX = 0, forceAY = 0, forceAZ = 0;
    double energy = 0;

    MLG_ASSERT(jStart < batchParams->BodyX.size(), "StartIndexB must be greater than StartIndexA");

    //VECTORIZE
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for(size_t j = jStart; j < jEnd; ++j)
    {
        const float dx = centerx[j] - ax;
        const float dy = centery[j] - ay;
        const float dz = centerz[j] - az;

        const float delta2 = (dx * dx) + (dy * dy) + (dz * dz);
        const float r2 = std::max(delta2, kMinDistance2);

        const float massProduct = am * (1.0f / invMasses[j]);
        const float invR = 1.0f / std::sqrt(r2);

        // F = G * (m1 * m2) / r^2
        // PE = -G * (m1 * m2) / r
        // PE = -F * r

        // Force magnitude at clamped distance.
        // Not actual distance if the bodies are closer than the sum of their radii, but this is a
        // common technique to avoid singularities in gravitational simulations.
        const float A = kGravitationalConstant * massProduct;
        const float F = A * invR * invR;
        const float PE = -A * invR;

        // Multiply the force magnitude by the normalized direction vector between centers.
        // Outside the min distance the length of the direction vector is 1.
        // Inside the min distance the length of the direction vector is r/R,
        // where r is the distance between centers and R is the sum of the radii.
        // This provides a softening effect as distance between centers approaches zero,
        // preventing singularities and extreme accelerations.
        const float forceX = F * dx * invR;
        const float forceY = F * dy * invR;
        const float forceZ = F * dz * invR;

        forceAX += forceX;
        forceAY += forceY;
        forceAZ += forceZ;

        // Inside the minimum distance, the actual force magnitude is A*r/R^3.
        // Since F = -dU/dr, integrating gives U = A*r^2/(2*R^3) + C.
        // Choosing C so this meets the ordinary potential, U(R) = -A/R, gives
        // U = A*r^2/(2*R^3) - 3*A/(2*R). The expression below combines that
        // softened potential with PE = -A/r outside the minimum distance.
        const float pe = PE - (0.5f * A * invR * (1.0f - (delta2 / r2)));
        energy += pe;

        fx[j] -= forceX;
        fy[j] -= forceY;
        fz[j] -= forceZ;
    }

    fx[i] += forceAX;
    fy[i] += forceAY;
    fz[i] += forceAZ;

    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    batchParams->PotentialEnergyy += energy;
}

void
ApplyGravityBatch(ApplyGravityBatchParams* batchParams)
{
    batchParams->PotentialEnergyy = 0;

    size_t count = 0;

    size_t j = batchParams->StartIndexB;

    for(size_t i = batchParams->StartIndexA;
        i < batchParams->BodyX.size() && count < batchParams->BatchSize;
        ++i, j = i + 1)
    {
        const size_t jStart = j;
        const size_t jEnd =
            std::min(batchParams->BodyX.size(), jStart + (batchParams->BatchSize - count));
        ApplyGravityRow(batchParams, i, jStart, jEnd);

        count += (jEnd - jStart);
    }

    batchParams->FinishCounter->fetch_add(1, std::memory_order_release);
    batchParams->FinishCounter->notify_all();
}

// Returns the total potential energy of the system after applying gravity.
void
ApplyGravity(Level& level, ThreadPool& threadPool)
{
    MLG_SCOPED_TIMER("Physics.ApplyGravity");

    const std::span physNodes = level.GetAllPhysicsNodes();

    const size_t numPairs = physNodes.size() * (physNodes.size() - 1) / 2;
    const size_t workerCount = threadPool.GetWorkerCount();
    const size_t batchSize = (numPairs / workerCount) + (numPairs % workerCount != 0 ? 1 : 0);
    const size_t numBatches = (numPairs / batchSize) + (numPairs % batchSize != 0 ? 1 : 0);

    std::vector<std::vector<float>> forceX(numBatches, std::vector<float>(physNodes.size(), 0));
    std::vector<std::vector<float>> forceY(numBatches, std::vector<float>(physNodes.size(), 0));
    std::vector<std::vector<float>> forceZ(numBatches, std::vector<float>(physNodes.size(), 0));

    std::vector<float> posArrays[3]//
    {
        std::vector<float>(physNodes.size()),
        std::vector<float>(physNodes.size()),
        std::vector<float>(physNodes.size()),
    };

    std::vector<float> invMassesArray(physNodes.size());

    const VVec3 positions{ .X = posArrays[0], .Y = posArrays[1], .Z = posArrays[2] };
    const std::span invMasses(invMassesArray);

    for(size_t i = 0; i < physNodes.size(); ++i)
    {
        const PhysicsNode& node = physNodes[i];
        const Vec3f lv = node.GetPosition();
        positions.X[i] = lv.x;
        positions.Y[i] = lv.y;
        positions.Z[i] = lv.z;
        invMasses[i] = node.GetInverseMass();
    }

    size_t pairCount = 0;

    std::atomic<size_t> finishCounter;
    std::vector<ApplyGravityBatchParams> batches;
    batches.reserve(numBatches);

    size_t startIndexA = 0, startIndexB = 1;

    for(size_t i = 0; i < physNodes.size(); ++i)
    {
        if(invMasses[i] == 0)
        {
            // Skip infinite mass bodies
            continue;
        }

        for(size_t j = i + 1; j < physNodes.size(); ++j, ++pairCount)
        {
            if(invMasses[j] == 0)
            {
                // Skip infinite mass bodies
                continue;
            }

            if(pairCount >= batchSize)
            {
                const ApplyGravityBatchParams batchParams //
                    {
                        .StartIndexA = startIndexA,
                        .StartIndexB = startIndexB,
                        .BatchSize = pairCount,
                        .BodyX = positions.X,
                        .BodyY = positions.Y,
                        .BodyZ = positions.Z,
                        .InvMasses = invMasses,
                        .ForceX = forceX[batches.size()],
                        .ForceY = forceY[batches.size()],
                        .ForceZ = forceZ[batches.size()],
                        .FinishCounter = &finishCounter,
                    };

                ApplyGravityBatchParams& params = batches.emplace_back(batchParams);

                if constexpr(kApplyGravityMultithreaded)
                {
                    threadPool.Enqueue<ApplyGravityBatch>(&params);
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
                .BodyX = positions.X,
                .BodyY = positions.Y,
                .BodyZ = positions.Z,
                .InvMasses = invMasses,
                .ForceX = forceX[batches.size()],
                .ForceY = forceY[batches.size()],
                .ForceZ = forceZ[batches.size()],
                .FinishCounter = &finishCounter,
            };

        ApplyGravityBatchParams& params = batches.emplace_back(batchParams);

        if constexpr(kApplyGravityMultithreaded)
        {
            threadPool.Enqueue<ApplyGravityBatch>(&params);
        }
        else
        {
            ApplyGravityBatch(&params);
        }
    }

    if constexpr(kApplyGravityMultithreaded)
    {

        size_t finishCount = finishCounter.load();
        while(finishCount < batches.size())
        {
            finishCounter.wait(finishCount);
            finishCount = finishCounter.load();
        }
    }

    MLG_ASSERT(batches.size() == numBatches);

    double totalPotentialEnergy = 0;

    for(size_t i = 0; i < physNodes.size(); ++i)
    {
        float fx = 0, fy = 0, fz = 0;
        for(const auto& batch : batches)
        {
            fx += batch.ForceX[i];
            fy += batch.ForceY[i];
            fz += batch.ForceZ[i];
        }
        physNodes[i].AddForce(Vec3f(fx, fy, fz));
    }

    for(const auto& batch : batches)
    {
        totalPotentialEnergy += batch.PotentialEnergyy;
    }

    PerfCounterGlobals::TotalPE.Set(totalPotentialEnergy);
}

void
ApplyExplosionImpulse(Level& level, const float magnitude)
{
    constexpr unsigned kRngSeed = 12345;
    std::mt19937 gen(kRngSeed);
    std::uniform_real_distribution<float> dis(0.5, 1);
    std::bernoulli_distribution sign;

    for(PhysicsNode& node : level.GetAllPhysicsNodes())
    {
        // Randomize the direction of the impulse.
        const Vec3f normal //
            {
                dis(gen) * (sign(gen) ? 1.0f : -1.0f),
                dis(gen) * (sign(gen) ? 1.0f : -1.0f),
                dis(gen) * (sign(gen) ? 1.0f : -1.0f),
            };

        const Vec3f impulse = normal.Normalize() * magnitude;
        node.ApplyImpulse(impulse);
    }
}

void
StopAll(Level& level)
{
    constexpr Vec3f zeroVelocity{ 0};

    for(PhysicsNode& node : level.GetAllPhysicsNodes())
    {
        node.SetLinearVelocity(zeroVelocity);
    }
}

float
ComputeKineticEnergy(const Level& level)
{
    float kineticEnergy = 0.0f;

    const std::span<const PhysicsNode> nodes = level.GetAllPhysicsNodes();

    std::vector<float> linearVelocitiesArrays[3] //
    {
        std::vector<float>(nodes.size()),
        std::vector<float>(nodes.size()),
        std::vector<float>(nodes.size()),
    };
    std::vector<float> invMassesArray(nodes.size());

    const VVec3 linearVelocities //
        {
            .X = linearVelocitiesArrays[0],
            .Y = linearVelocitiesArrays[1],
            .Z = linearVelocitiesArrays[2],
        };
    const std::span<float> invMasses(invMassesArray);

    for(size_t i = 0; i < nodes.size(); ++i)
    {
        const PhysicsNode& node = nodes[i];
        linearVelocities.X[i] = node.GetLinearVelocity().x;
        linearVelocities.Y[i] = node.GetLinearVelocity().y;
        linearVelocities.Z[i] = node.GetLinearVelocity().z;
        invMasses[i] = node.GetInverseMass();
    }

    auto range = std::views::zip(invMasses, linearVelocities.X, linearVelocities.Y, linearVelocities.Z);

    for(const auto& [invMass, vx, vy, vz] : range)
    {
        if(0 == invMass)
        {
            continue; // Skip infinite mass bodies
        }

        const float speedSq = (vx * vx) + (vy * vy) + (vz * vz);
        // KE = mv^2 / 2
        kineticEnergy += 0.5f * speedSq / invMass;
    }

    return kineticEnergy;
}

Result<>
MainLoop()
{
    CameraActor cameraActor;
    DevUi devUi;

    auto task = System::Create(kAppName);
    MLG_CHECK(task, "Failed to create System");

    while(!task->IsComplete())
    {
        MLG_CHECK(task->Update());
    }

    MLG_CHECK(task->Succeeded(), "System creation failed");
    auto systemResult = task->Get();
    MLG_CHECK(systemResult, "Failed to get System instance");

    System& system = *systemResult;
    GpuHelper& gpuHelper = system.GetGpuHelper();
    ThreadPool& threadPool = system.GetThreadPool();
    FileFetcher& fileFetcher = system.GetFileFetcher();
    Renderer& renderer = system.GetRenderer();
    const ImGuiRenderer& imGuiRenderer = system.GetImGuiRenderer();

    auto loadResult = LoadLevel(gpuHelper, threadPool, fileFetcher);
    MLG_CHECK(loadResult);

    auto&& [propKit, level] = std::move(*loadResult);

    auto sceneResult = Scene::Create(gpuHelper, level);
    MLG_CHECK(sceneResult);

    Scene scene = std::move(*sceneResult);

    ApplyRandomVelocities(level);

    constexpr float kInitialCameraDistance = 40.0f;

    TrTransformf cameraXForm{ .T{ 0, 0, -kInitialCameraDistance } };
    //Camera camera((Viewport(gpuHelper.GetScreenDimensions())));

    cameraActor.SetTransform(cameraXForm);
    cameraActor.SetViewport(Viewport(gpuHelper.GetScreenDimensions()));

    static constexpr float kMouseWheelScale = 20.0f;

    bool pauseSim = false;

    const ActionMapping actionMappings[] //
        {
            {
                .ActionId = quit,
                .Input = InputButton::KeyPressed(SDL_SCANCODE_ESCAPE),
            },
            {
                .ActionId = moveForward,
                .Input = InputButton::KeyDown(SDL_SCANCODE_W),
                .Scale = 1,
            },
            {
                .ActionId = moveBackward,
                .Input = InputButton::KeyDown(SDL_SCANCODE_S),
                .Scale = -1,
            },
            {
                .ActionId = moveLeft,
                .Input = InputButton::KeyDown(SDL_SCANCODE_A),
                .Scale = -1,
            },
            {
                .ActionId = moveRight,
                .Input = InputButton::KeyDown(SDL_SCANCODE_D),
                .Scale = 1,
            },
            {
                .ActionId = lookLeftRight,
                .Input = InputAxis::MouseMoveX,
                .Scale = CameraActor::kDefaultRotPerMouseMove * 2 * std::numbers::pi_v<float>,
            },
            {
                .ActionId = lookUpDown,
                .Input = InputAxis::MouseMoveY,
                .Scale = CameraActor::kDefaultRotPerMouseMove * 2 * std::numbers::pi_v<float>,
            },
            {
                .ActionId = moveUpDown,
                .Input = InputAxis::MouseWheelY,
                .Scale = kMouseWheelScale,
            },
            {
                .ActionId = captureMouse,
                .Input = InputButton::MousePressed(SDL_BUTTON_LEFT),
            },
            {
                .ActionId = releaseMouse,
                .Input = InputButton::MouseReleased(SDL_BUTTON_LEFT),
            },
            {
                .ActionId = explode,
                .Input = InputButton::KeyPressed(SDL_SCANCODE_RETURN),
            },
            {
                .ActionId = stopAll,
                .Input = InputButton::KeyPressed(SDL_SCANCODE_BACKSPACE),
            },
            {
                .ActionId = pause,
                .Input = InputButton::KeyPressed(SDL_SCANCODE_F1),
            },
        };

    InputMapper inputMapper(actionMappings);

    Timer frameTimer;

    bool isCameraActorActive = false;

    while(!system.ShouldQuit())
    {
        MLG_SCOPED_TIMER(" Frame");

        const float elapsedSeconds = frameTimer.GetElapsedSeconds();

        frameTimer.Restart();

        inputMapper.BeginFrame();

        auto eventHandlerFunc = [](const SDL_Event& sdlEvent, InputMapper* im)
        {
            im->ProcessEvent(sdlEvent);
            return EventDisposition::Process;
        };

        const EventHandler eventHandler(+eventHandlerFunc, &inputMapper);

        system.ProcessEvents(eventHandler);

        inputMapper.EndFrame();

        if(system.IsMinimized())
        {
            std::this_thread::yield();
            continue;
        }

        if(system.ShouldQuit())
        {
            break;
        }

        if(system.WasMinimized()
            || system.WasRestored()
            || system.WasFocusGained()
            || system.WasFocusLost())
        {
            inputMapper.Clear();
        }

        if(inputMapper.Action(quit))
        {
            System::PostQuitEvent();
        }
        if(inputMapper.Action(captureMouse))
        {
            isCameraActorActive = true;
            system.SetMouseCaptured(true);
        }
        if(inputMapper.Action(releaseMouse))
        {
            isCameraActorActive = false;
            system.SetMouseCaptured(false);
        }
        if(inputMapper.Action(explode))
        {
            constexpr float kImpulseMagnitude = 5.0f;
            ApplyExplosionImpulse(level, kImpulseMagnitude);
        }
        if(inputMapper.Action(stopAll))
        {
            StopAll(level);
        }
        if(inputMapper.Action(pause))
        {
            pauseSim = !pauseSim;
        }

        if(!pauseSim)
        {
            level.Update(kPhysicsTimeStep);
            ApplyGravity(level, threadPool);

            const float kineticEnergy = ComputeKineticEnergy(level);
            const double totalEnergy = kineticEnergy + PerfCounterGlobals::TotalPE.GetValue();

            PerfCounterGlobals::TotalKE.Set(kineticEnergy);
            PerfCounterGlobals::TotalEnergy.Set(totalEnergy);
        }

        MLG_CHECK(scene.SyncFromLevel());

        if(isCameraActorActive)
        {
            cameraActor.Update(inputMapper, elapsedSeconds);
        }
        cameraXForm = cameraActor.GetTransform();

        MLG_CHECK(scene.SyncToGpu(gpuHelper.GetDevice()));

        auto target = gpuHelper.GetSwapChainTexture();
        MLG_CHECKV(target, "Failed to get swap chain texture");

        if(ImGui::GetFrameCount() > 1)
        {
            // ImGui must render at least one frame to calculate panel sizes.

            const Rect& scenePanelRect = devUi.GetScenePanelRect();

            const Viewport sceneViewport(scenePanelRect.GetDimensions());
            cameraActor.SetViewport(sceneViewport);

            MLG_CHECK(renderer.Render(cameraActor.GetCamera(), cameraXForm, scene, propKit));
            MLG_CHECK(renderer.Composite(*target, scenePanelRect));
        }

        auto renderGui = [&]() { return devUi.Render(); };

        MLG_CHECK(imGuiRenderer.Render(gpuHelper.GetDevice(), *target, renderGui));

        {
#if !defined(__EMSCRIPTEN__)
            MLG_SCOPED_TIMER("Present");
            MLG_CHECK(gpuHelper.GetSurface().Present(), "Failed to present backbuffer");
#endif
        }
    }

    PerfMetrics::LogCounters();

    return Result<>::Ok;
}
} // namespace

int
main(int /*argc*/, char** /*argv*/)
{
    if(!MainLoop())
    {
        return -1;
    }

    return 0;
}
