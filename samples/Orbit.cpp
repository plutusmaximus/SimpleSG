#include "Camera.h"
#include "DevUi.h"
#include "GpuHelper.h"
#include "ImGuiRenderer.h"
#include "InputMapper.h"
#include "Level.h"
#include "LuaRuntime.h"
#include "MouseNav.h"
#include "PerfMetrics.h"
#include "PhysicsLevel.h"
#include "PropKit.h"
#include "RangeQuery.h"
#include "Renderer.h"
#include "Scene.h"
#include "ShapeMeshDefs.h"
#include "System.h"
#include "ThreadPool.h"

#include <filesystem>
#include <imgui.h>
#include <random>
#include <ranges>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_scancode.h>
#include <thread>
#include <vector>

namespace
{
constexpr const char* kAppName = "Orbit";

constexpr float kPhysicsFps = 60.0f;
constexpr float kPhysicsTimeStep = 1.0f / kPhysicsFps;
constexpr float kGravitationalConstant = 0.1f; // 6.674e-11f;//(m^3 kg^-1 s^-2)

constexpr bool kApplyGravityMultithreaded = true;
struct PerfCounterGlobals
{
    static inline PerfCounter TotalPE{ { .Name = "Energy.PE" } }; // Potential Energy
    static inline PerfCounter TotalKE{ { .Name = "Energy.KE" } }; // Kinetic Energy
    static inline PerfCounter TotalEnergy{ { .Name = "Energy.Total" } };
};

class NodeActivator
{
public:
    NodeActivator() = delete;

    NodeActivator(Level& level, const bool activate)
        : m_Level(level),
          m_Activate(activate)
    {
    }

    const Level::Node& operator()(const Level::Node& node) const
    {
        m_Level.SetActive(node, m_Activate);

        return node; // NOLINT
    }

    const Level::Node* operator()(const Level::Node* node) const
    {
        operator()(*node);
        return node;
    }

private:
    Level& m_Level; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    bool m_Activate;
};

class NodeVisibility
{
public:
    NodeVisibility() = delete;

    NodeVisibility(Level& level, const bool visible)
        : m_Level(level),
          m_Visible(visible)
    {
    }

    const Level::Node& operator()(const Level::Node& node) const
    {
        m_Level.SetVisible(node, m_Visible);

        return node; // NOLINT
    }

    const Level::Node* operator()(const Level::Node* node) const
    {
        operator()(*node);
        return node;
    }

private:
    Level& m_Level; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
    bool m_Visible;
};

class NodeSelector
{
public:
    NodeSelector() = delete;

    NodeSelector(const Camera& camera, const TrTransformf& cameraXForm, const Rect& selectionRect)
        : m_Frustum(camera, cameraXForm, selectionRect)
    {
    }

    bool operator()(const Level::Node& node) const
    {
        const BoundingSphere bs = node.GetBoundingSphere();
        const Frustum::ContainsResult result = m_Frustum.Contains(bs);

        return result == Frustum::ContainsResult::Inside
            || result == Frustum::ContainsResult::Intersects;
    }

    bool operator()(const Level::Node* node) const { return operator()(*node); }

private:
    Frustum m_Frustum;
};

Result<std::tuple<PropKit, Level>>
Load(GpuHelper& gpuHelper, ThreadPool& threadPool, FileFetcher& fileFetcher)
{
    constexpr float kBallRadius = 1.0f;

    const PropKitDef propKitDef //
        {
            .ModelDefs //
            {
                {
                    .Name{ "Shape" },
                    .MeshDefs //
                    {
                        ShapeMeshDefs::Ball({ .Radius = kBallRadius }),
                    },
                },
            },
        };

    auto propKit =
        PropKit::Create(gpuHelper, threadPool, fileFetcher, std::filesystem::path{}, propKitDef);
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
    for(size_t i = 0; i < NUM_BODIES; ++i)
    {
        const float radius = MIN_RADIUS + (std::abs(dis(gen)) * (MAX_RADIUS - MIN_RADIUS));
        const float mass = radius;
        const Vec3f position{ dis(gen) * GRID_SIZE, dis(gen) * GRID_SIZE, dis(gen) * GRID_SIZE };

        LevelNodeDef nodeDef //
            {
                .Name{ std::format("Body{}", i) },
                .Transform{ .T{ position }, .S{ radius } },
                .Components //
                {
                    .Model = ModelRef{ .Name = "Shape" },
                    .Body =
                        RigidBodyDef //
                    {
                        .Mass{ mass },
                        .BoundingVolume =
                            BoundingVolumeDef{ SphereDef{ .Center = Vec3f(0), .Radius = radius } },
                    },
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

void
ApplyRandomVelocities(PhysicsLevel& physLevel)
{
    constexpr float MAX_SPEED = 0.5f;
    constexpr float MIN_SPEED = 0.1f;
    constexpr unsigned kRngSeed = 12345;

    std::mt19937 gen(kRngSeed);
    std::uniform_real_distribution<float> dis(-1, 1);

    for(auto& vel : physLevel.GetLinearVelocities())
    {
        const Vec3f randomVel = Vec3f{ dis(gen), dis(gen), dis(gen) }.Normalize()
            * (MIN_SPEED + (std::abs(dis(gen)) * (MAX_SPEED - MIN_SPEED)));

        vel = randomVel;
    }
}

struct ApplyGravityBatchParams
{
    size_t StartIndexA{ 0 };
    size_t StartIndexB{ 0 };
    size_t BatchSize{ 0 };

    std::span<float> BodyX;
    std::span<float> BodyY;
    std::span<float> BodyZ;
    std::span<float> BodyRadius;
    std::span<float> BodyMass;

    std::span<float> ForceX;
    std::span<float> ForceY;
    std::span<float> ForceZ;
    
    float PotentialEnergy{ 0 };

    std::atomic<size_t>* FinishCounter{ nullptr };
};

[[maybe_unused]] void
ApplyGravityRow2(ApplyGravityBatchParams* batchParams, const size_t i, const size_t jStart, const size_t jEnd)
{
    const float ax = batchParams->BodyX[i];
    const float ay = batchParams->BodyY[i];
    const float az = batchParams->BodyZ[i];
    const float ar = batchParams->BodyRadius[i];
    const float am = batchParams->BodyMass[i];

    const float* __restrict bx = batchParams->BodyX.data();
    const float* __restrict by = batchParams->BodyY.data();
    const float* __restrict bz = batchParams->BodyZ.data();
    const float* __restrict br = batchParams->BodyRadius.data();
    const float* __restrict bm = batchParams->BodyMass.data();

    float* __restrict fx = batchParams->ForceX.data();
    float* __restrict fy = batchParams->ForceY.data();
    float* __restrict fz = batchParams->ForceZ.data();

    float forceAX = 0, forceAY = 0, forceAZ = 0;
    float energy = 0;

    MLG_ASSERT(jStart < batchParams->BodyX.size(), "StartIndexB must be greater than StartIndexA");

    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    for(size_t j = jStart; j < jEnd; ++j)
    {
        const float dx = bx[j] - ax;
        const float dy = by[j] - ay;
        const float dz = bz[j] - az;

        const float minR = ar + br[j];
        const float minR2 = minR * minR;
        const float delta2 = (dx * dx) + (dy * dy) + (dz * dz);
        const float r2 = std::max(delta2, minR2);

        const float massProduct = am * bm[j];
        const float invR = 1.0f / std::sqrt(r2);

        const float pe = -kGravitationalConstant * massProduct * invR;

        const float scale = -pe / r2;

        const float forceX = scale * dx;
        const float forceY = scale * dy;
        const float forceZ = scale * dz;

        forceAX += forceX;
        forceAY += forceY;
        forceAZ += forceZ;
        energy += pe;

        fx[j] -= forceX;
        fy[j] -= forceY;
        fz[j] -= forceZ;
    }

    fx[i] += forceAX;
    fy[i] += forceAY;
    fz[i] += forceAZ;

    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    batchParams->PotentialEnergy += energy;
}

void
ApplyGravityBatch(ApplyGravityBatchParams* batchParams)
{
    batchParams->PotentialEnergy = 0;

    size_t count = 0;

    size_t j = batchParams->StartIndexB;

    for(size_t i = batchParams->StartIndexA;
        i < batchParams->BodyX.size() && count < batchParams->BatchSize;
        ++i, j = i + 1)
    {
        const size_t jStart = j;
        const size_t jEnd =
            std::min(batchParams->BodyX.size(), jStart + (batchParams->BatchSize - count));
        ApplyGravityRow2(batchParams, i, jStart, jEnd);

        count += (jEnd - jStart);
    }

    batchParams->FinishCounter->fetch_add(1, std::memory_order_relaxed);
}

// Returns the total potential energy of the system after applying gravity.
void
ApplyGravity(PhysicsLevel& physLevel, ThreadPool& threadPool)
{
    MLG_SCOPED_TIMER("Physics.ApplyGravity");

    const std::span<const RigidBody> bodies = physLevel.GetBodies();
    const std::span<const TrsTransformf> transforms = physLevel.GetTransforms();

    const size_t numPairs = bodies.size() * (bodies.size() - 1) / 2;
    const size_t workerCount = threadPool.GetWorkerCount();
    const size_t batchSize = (numPairs / workerCount) + (numPairs % workerCount != 0 ? 1 : 0);
    const size_t numBatches = (numPairs / batchSize) + (numPairs % batchSize != 0 ? 1 : 0);

    std::vector<float> centerX(bodies.size());
    std::vector<float> centerY(bodies.size());
    std::vector<float> centerZ(bodies.size());
    std::vector<float> radius(bodies.size());
    std::vector<float> mass(bodies.size());
    std::vector<float> forceX(bodies.size());
    std::vector<float> forceY(bodies.size());
    std::vector<float> forceZ(bodies.size());

    for(size_t i = 0; i < bodies.size(); ++i)
    {
        const RigidBody& body = bodies[i];
        const BoundingSphere& sphere = body.GetBoundingSphere();
        const Vec3f center = transforms[i] * sphere.GetCenter();
        centerX[i] = center.x;
        centerY[i] = center.y;
        centerZ[i] = center.z;
        radius[i] = sphere.GetRadius();
        mass[i] = body.GetMass().Value();
        forceX[i] = 0;
        forceY[i] = 0;
        forceZ[i] = 0;
    }

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
                        .BodyX = centerX,
                        .BodyY = centerY,
                        .BodyZ = centerZ,
                        .BodyRadius = radius,
                        .BodyMass = mass,
                        .ForceX = forceX,
                        .ForceY = forceY,
                        .ForceZ = forceZ,
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
                .BodyX = centerX,
                .BodyY = centerY,
                .BodyZ = centerZ,
                .BodyRadius = radius,
                .BodyMass = mass,
                .ForceX = forceX,
                .ForceY = forceY,
                .ForceZ = forceZ,
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
        while(finishCounter.load(std::memory_order_relaxed) < batches.size())
        {
            std::this_thread::yield();
        }
    }

    MLG_ASSERT(batches.size() == numBatches);

    float totalPotentialEnergy = 0;

    for(size_t i = 0; i < bodies.size(); ++i)
    {
        physLevel.AddForce(i, Vec3f(forceX[i], forceY[i], forceZ[i]));
    }

    for(const auto& params : batches)
    {
        totalPotentialEnergy += params.PotentialEnergy;
    }

    PerfCounterGlobals::TotalPE.Set(totalPotentialEnergy);
}

void
ApplyExplosionImpulse(PhysicsLevel& physLevel, const float magnitude)
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

void
ApplyStoppingImpulse(PhysicsLevel& physLevel)
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

float
ComputeKineticEnergy(const PhysicsLevel& physLevel)
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

    return kineticEnergy;
}

Result<>
MainLoop()
{
    WalkMouseNav mouseNav;
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

    auto loadResult = Load(gpuHelper, threadPool, fileFetcher);
    MLG_CHECK(loadResult);

    auto&& [propKit, level] = std::move(*loadResult);

    auto sceneResult = Scene::Create(gpuHelper, level);
    MLG_CHECK(sceneResult);

    Scene scene = std::move(*sceneResult);

    auto physLevelResult = PhysicsLevel::Create(level, threadPool);
    MLG_CHECK(physLevelResult);

    PhysicsLevel physLevel = std::move(*physLevelResult);

    ApplyRandomVelocities(physLevel);

    constexpr float kInitialCameraDistance = 40.0f;

    TrTransformf cameraXForm{ .T{ 0, 0, -kInitialCameraDistance } };
    Camera camera((Viewport(gpuHelper.GetScreenDimensions())));

    mouseNav.SetTransform(cameraXForm);

    constexpr ActionIdentifier quit("Quit");
    constexpr ActionIdentifier moveForward("MoveForward");
    constexpr ActionIdentifier moveBackward("MoveBackward");
    constexpr ActionIdentifier moveLeft("MoveLeft");
    constexpr ActionIdentifier moveRight("MoveRight");
    constexpr ActionIdentifier moveUpDown("MoveUpDown");
    constexpr ActionIdentifier lookLeftRight("LookLeftRight");
    constexpr ActionIdentifier lookUpDown("LookUpDown");
    constexpr ActionIdentifier captureMouse("CaptureMouse");
    constexpr ActionIdentifier releaseMouse("ReleaseMouse");
    constexpr ActionIdentifier explode("Explode");
    constexpr ActionIdentifier stopAll("StopAll");
    constexpr ActionIdentifier pause("Pause");
    constexpr ActionIdentifier activateNodes("ActivateNodes");

    static constexpr float kMouseWheelScale = 20.0f;

    bool pauseSim = false;
    bool activateSelectedNodes = false;

    const InputMapping inputMappings[] //
        {
            {
                .Input = InputButtons::KeyPressed(SDL_SCANCODE_ESCAPE),
                .ActionId = quit,
                .Handler = [&](const ActionEvent&) { System::PostQuitEvent(); },
            },
            {
                .Input = InputButtons::KeyDown(SDL_SCANCODE_W),
                .ActionId = moveForward,
                .Handler = [&](const ActionEvent& event)
                { mouseNav.Move(Vec3f(0, 0, event.Value)); },
                .Scale = 1,
            },
            {
                .Input = InputButtons::KeyDown(SDL_SCANCODE_S),
                .ActionId = moveBackward,
                .Handler = [&](const ActionEvent& event)
                { mouseNav.Move(Vec3f(0, 0, event.Value)); },
                .Scale = -1,
            },
            {
                .Input = InputButtons::KeyDown(SDL_SCANCODE_A),
                .ActionId = moveLeft,
                .Handler = [&](const ActionEvent& event)
                { mouseNav.Move(Vec3f(event.Value, 0, 0)); },
                .Scale = -1,
            },
            {
                .Input = InputButtons::KeyDown(SDL_SCANCODE_D),
                .ActionId = moveRight,
                .Handler = [&](const ActionEvent& event)
                { mouseNav.Move(Vec3f(event.Value, 0, 0)); },
                .Scale = 1,
            },
            {
                .Input = InputAxes::MouseMoveX,
                .ActionId = lookLeftRight,
                .Handler = [&](const ActionEvent& event) { mouseNav.Look(Vec2f(event.Value, 0)); },
                .Scale = WalkMouseNav::kDefualtRotPerDXY * 2 * std::numbers::pi_v<float>,
            },
            {
                .Input = InputAxes::MouseMoveY,
                .ActionId = lookUpDown,
                .Handler = [&](const ActionEvent& event) { mouseNav.Look(Vec2f(0, event.Value)); },
                .Scale = WalkMouseNav::kDefualtRotPerDXY * 2 * std::numbers::pi_v<float>,
            },
            {
                .Input = InputAxes::MouseWheelY,
                .ActionId = moveUpDown,
                .Handler = [&](const ActionEvent& event)
                { mouseNav.Move(Vec3f(0, event.Value, 0)); },
                .Scale = kMouseWheelScale,
            },
            {
                .Input = InputButtons::MousePressed(SDL_BUTTON_LEFT),
                .ActionId = captureMouse,
                .Handler =
                    [&](const ActionEvent&)
                {
                    mouseNav.Activate();
                    SDL_SetWindowRelativeMouseMode(gpuHelper.GetWindow(), true);
                },
            },
            {
                .Input = InputButtons::MouseReleased(SDL_BUTTON_LEFT),
                .ActionId = releaseMouse,
                .Handler =
                    [&](const ActionEvent&)
                {
                    mouseNav.Deactivate();
                    SDL_SetWindowRelativeMouseMode(gpuHelper.GetWindow(), false);
                },
            },
            {
                .Input = InputButtons::KeyPressed(SDL_SCANCODE_RETURN),
                .ActionId = explode,
                .Handler =
                    [&](const ActionEvent&)
                {
                    constexpr float kImpulseMagnitude = 5.0f;
                    ApplyExplosionImpulse(physLevel, kImpulseMagnitude);
                },
            },
            {
                .Input = InputButtons::KeyPressed(SDL_SCANCODE_BACKSPACE),
                .ActionId = stopAll,
                .Handler = [&](const ActionEvent&) { ApplyStoppingImpulse(physLevel); },
            },
            {
                .Input = InputButtons::KeyPressed(SDL_SCANCODE_F1),
                .ActionId = pause,
                .Handler = [&](const ActionEvent&) { pauseSim = !pauseSim; },
            },
            {
                .Input = InputButtons::KeyPressed(SDL_SCANCODE_F2),
                .ActionId = activateNodes,
                .Handler =
                    [&](const ActionEvent&)
                {
                    activateSelectedNodes = !activateSelectedNodes;

                    if(!activateSelectedNodes)
                    {
                        RangeQuery::from(physLevel.GetNodes())
                            .apply(NodeActivator(level, true))
                            .apply(NodeVisibility(level, true))
                            .exec();
                    }
                    else
                    {
                        // const Rect rect(Point(172, 290), Point(247, 360));
                        const Rect rect({ .X = 200, .Y = 400, .Width = 75, .Height = 75 });
                        // const Rect rect(Point(1000, 1000), Point(1050, 1050));

                        RangeQuery::from(physLevel.GetNodes())
                            .apply(NodeActivator(level, false))
                            .apply(NodeVisibility(level, false))
                            .where(NodeSelector(camera, cameraXForm, rect))
                            .apply(NodeActivator(level, true))
                            .apply(NodeVisibility(level, true))
                            .exec();
                    }
                },
            },
        };

    InputMapper inputMapper(inputMappings);

    Timer frameTimer;

    while(!system.ShouldQuit())
    {
        MLG_SCOPED_TIMER(" Frame");

        const float elapsedSeconds = frameTimer.GetElapsedSeconds();

        frameTimer.Restart();

        auto eventHandlerFunc = [](const SDL_Event& sdlEvent, InputMapper* im)
        {
            im->ProcessEvent(sdlEvent);
            return EventDisposition::Process;
        };

        const EventHandler eventHandler(+eventHandlerFunc, &inputMapper);

        system.ProcessEvents(eventHandler);

        if(system.IsMinimized())
        {
            std::this_thread::yield();
            continue;
        }

        if(system.ShouldQuit())
        {
            break;
        }

        inputMapper.DispatchEvents();

        if(!pauseSim)
        {
            ApplyGravity(physLevel, threadPool);

            physLevel.Update(kPhysicsTimeStep);

            const float kineticEnergy = ComputeKineticEnergy(physLevel);
            const double totalEnergy = kineticEnergy + PerfCounterGlobals::TotalPE.GetValue();

            PerfCounterGlobals::TotalKE.Set(kineticEnergy);
            PerfCounterGlobals::TotalEnergy.Set(totalEnergy);
        }

        MLG_CHECK(physLevel.SyncToLevel(level));

        MLG_CHECK(scene.SyncFromLevel());

        mouseNav.Update(elapsedSeconds);
        cameraXForm = mouseNav.GetTransform();

        MLG_CHECK(scene.SyncToGpu(gpuHelper.GetDevice()));

        auto target = gpuHelper.GetSwapChainTexture();
        MLG_CHECKV(target, "Failed to get swap chain texture");

        if(ImGui::GetFrameCount() > 1)
        {
            // ImGui must render at least one frame to calculate panel sizes.

            const Rect& scenePanelRect = devUi.GetScenePanelRect();

            const Viewport sceneViewport(scenePanelRect.GetDimensions());
            camera.SetViewport(sceneViewport);

            MLG_CHECK(renderer.Render(camera, cameraXForm, scene, propKit));
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