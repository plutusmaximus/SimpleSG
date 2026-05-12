#include "Compositor.h"
#include "Renderer.h"
#include "PropKit.h"
#include "ECS.h"
#include "GltfLoader.h"
#include "ImGuiRenderer.h"
#include "Level.h"
#include "MouseNav.h"
#include "PerfMetrics.h"
#include "Projection.h"
#include "Scene.h"
#include "scope_exit.h"
#include "Shapes.h"
#include "Stopwatch.h"
#include "WebgpuHelper.h"
#include "VecMath.h"

#include <algorithm>
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <random>
#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <thread>

static constexpr const char* APP_NAME = "Orbit";

static constexpr float PHYSICS_FPS = 100.0f;
static constexpr float RENDER_FPS = 60.0f;

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

static Result<> RenderGui();

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

    std::vector<LevelNodeDef> nodeDefs;
    nodeDefs.reserve(1000);
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
                .Body = RigidBodyDef{ .Velocity{ velocity }, .Mass{ mass } },
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

struct Simulation
{
    static Result<> Create(const Level& level, Simulation& outSim);

    Simulation() = default;
    Simulation(const Simulation&) = delete;
    Simulation& operator=(const Simulation&) = delete;
    Simulation(Simulation&& other) = default;
    Simulation& operator=(Simulation&& other) = default;

    void DoCollisions(const float dt);

    void Integrate(const float dt);

    Result<> SyncToLevel(Level& level);

private:

    Simulation(std::vector<Level::NodeHandle>&& nodeHandles,
        std::vector<TrsTransformf>&& transforms,
        std::vector<RigidBody>&& bodies,
        std::vector<Collider>&& colliders)
        : m_NodeHandles(std::move(nodeHandles)),
          m_Transforms(std::move(transforms)),
          m_Bodies(std::move(bodies)),
          m_Colliders(std::move(colliders))
    {
    }

public:

    std::vector<Level::NodeHandle> m_NodeHandles;
    std::vector<TrsTransformf> m_Transforms;
    std::vector<RigidBody> m_Bodies;
    std::vector<Collider> m_Colliders;
};

struct HitResult
{
    float TimeOfImpact;
    Vec3f ContactPoint;
    Vec3f ContactNormal;
};

Result<>
Simulation::Create(const Level& level, Simulation& outSim)
{
    std::span<const Level::NodeHandle> allHandles = level.GetAllHandles();

    size_t count = 0;
    for(const auto& handle : allHandles)
    {
        const auto& node = level.GetNode(handle);
        if(node->Components.Body)
        {
            ++count;
        }
    }

    std::vector<Level::NodeHandle> nodeHandles;
    std::vector<TrsTransformf> transforms;
    std::vector<RigidBody> bodies;
    std::vector<Collider> colliders;
    nodeHandles.reserve(count);
    transforms.reserve(count);
    bodies.reserve(count);
    colliders.reserve(count);

    for(const auto& handle : allHandles)
    {
        const auto& node = level.GetNode(handle);
        MLG_ASSERT((node->Components.Body && node->Components.Collider)
            || (!node->Components.Body && !node->Components.Collider),
            "Node {} has Body component but no Collider, or vice versa",
            node->Name);

        if(!node->Components.Body)
        {
            continue;
        }

        nodeHandles.emplace_back(handle);
        transforms.emplace_back(node->LocalTransform);
        bodies.emplace_back(*node->Components.Body);
        colliders.emplace_back(*node->Components.Collider);
    }

    Simulation sim(std::move(nodeHandles),
        std::move(transforms),
        std::move(bodies),
        std::move(colliders));

    outSim = std::move(sim);

    return Result<>::Ok;
}

void
Simulation::Integrate(const float dt)
{
    for (size_t i = 0; i < m_Bodies.size(); ++i)
    {
        // Update position using velocity and acceleration from
        // previous time step.
        RigidBody& b = m_Bodies[i];
        TrsTransformf& trs = m_Transforms[i];
        const Vec3f a0 = b.Force0 / b.Mass.Value();
        // p = ∫ v dt
        // v = v0 + a * t
        // p1 = ∫ (v0 + a * t) dt = v0 * dt + (a * dt^2) / 2 + p0
        trs.T += ((a0 * dt * dt) / 2) + b.Velocity * dt;

        // Update velocity using average of acceleration from previous and current time step.
        const Vec3f a1 = b.Force1 / b.Mass.Value();
        b.Velocity += (a1 + a0) * dt / 2;

        b.Force0 = b.Force1;
        b.Force1 = Vec3f{ 0 };
    }
}

Result<>
Simulation::SyncToLevel(Level& level)
{
    for(size_t i = 0; i < m_NodeHandles.size(); ++i)
    {
        const auto& handle = m_NodeHandles[i];
        const TrsTransformf& xform = m_Transforms[i];
        MLG_CHECK(level.UpdateLocalTransform(handle, xform));
    }

    return Result<>::Ok;
}

static bool
SphereSphereSweep(const TrsTransformf& tA,
    const RigidBody& bodyA,
    const Collider& colliderA,
    const TrsTransformf& tB,
    const RigidBody& bodyB,
    const Collider& colliderB,
    const float dt,
    HitResult& outHitResult)
{
    constexpr float EPSILON = 1e-6f;
    constexpr float EPSILON_SQ = EPSILON * EPSILON;

    // Swept sphere test: check if the spheres will collide during this time step, and if so, fill
    // outHitResult with the time of impact (0 to 1), contact point, and contact normal.

    // Expand sphere B by the radius of sphere A, and treat sphere A as a point moving along its
    // velocity vector. This simplifies the problem to a ray-sphere intersection test.

    // We need to solve the quadratic equation for the time of impact t:
    // ||(p + v * t) - c||^2 = r^2
    // Or:
    // (p + v * t).Dot(p + v * t) = r * r
    // which expands to:
    // (v.Dot(v)) * t^2 + (2 * p.Dot(v)) * t + (p.Dot(p) - r^2) = 0
    // Or:
    // a * t^2 + b * t + c = 0
    // Where:
    // a = v.Dot(v)
    // b = 2 * p.Dot(v)
    // c = p.Dot(p) - r^2

    const float radiusA = std::get<SphereCollider>(colliderA.Shape).Radius;
    const float radiusB = std::get<SphereCollider>(colliderB.Shape).Radius;

    const Vec3 v = bodyA.Velocity - bodyB.Velocity;

    // a * t^2
    // First term of the quadratic equation, without the t^2.
    // Squared relative velocity.
    const float a = v.Dot(v);

    // (p.Dot(p) - r^2)
    // Third term of the quadratic equation.
    // Squared distance between the centers minus the squared sum of the radii.
    const float r = radiusA + radiusB;
    const Vec3 p = tA.T - tB.T;
    const float lenSq = p.Dot(p);
    const float c = lenSq - r * r;
    if(c <= 0)
    {
        // Overlapping.
        outHitResult.TimeOfImpact = 0.0f;

        // Contact normal is the normalized vector between centers at the start of the time step.
        // Unless the centers are extremely close, in which case we can try using the relative
        // velocity to determine the contact normal.

        if(lenSq < EPSILON_SQ)
        {
            // Centers are extremely close.  Try setting contact normal based on relative velocity.
            if (a > EPSILON_SQ)
            {
                outHitResult.ContactNormal = v / std::sqrtf(a);
            }
            else
            {
                // Relative velocity is also extremely small.  Just pick an arbitrary contact normal.
                outHitResult.ContactNormal = Vec3f{ 1, 0, 0 };
            }
        }
        else
        {
            outHitResult.ContactNormal = p / std::sqrtf(lenSq);
        }

        outHitResult.ContactPoint = tB.T + outHitResult.ContactNormal * radiusB;

        return true;
    }

    // No relative motion.
    if (a < EPSILON_SQ)
    {
        return false;
    }

    const float b = 2.0f * p.Dot(v);

    // Moving apart.
    if (b >= 0.0f)
    {
        return false;
    }

    // Solve quadratic equation for time of impact.
    // t = (-b (+/-) sqrt(b^2 - 4ac)) / (2a)

    const float discriminant = b * b - 4.0f * a * c;

    if (discriminant < EPSILON)
    {
        return false;
    }

    const float sqrtD = std::sqrtf(discriminant);

    // We only care about the minus sqrt.  It's the entry point.
    // The plus sqrt would be the exit point.
    const float t = (-b - sqrtD) / (2.0f * a);

    if (t < 0.0f || t > dt)
    {
        return false;
    }

    // Centers at time of impact
    const Vec3f centerA = tA.T + bodyA.Velocity * t;
    const Vec3f centerB = tB.T + bodyB.Velocity * t;

    const Vec3f n = centerA - centerB;
    const float nLenSq = n.Dot(n);

    outHitResult.TimeOfImpact = t;
    outHitResult.ContactNormal =
        nLenSq > EPSILON_SQ ? n / std::sqrtf(nLenSq) : Vec3f{ 1.0f, 0.0f, 0.0f };
    const Vec3f hitA = centerA - outHitResult.ContactNormal * radiusA;
    const Vec3f hitB = centerB + outHitResult.ContactNormal * radiusB;

    outHitResult.ContactPoint = (hitA + hitB) * 0.5f;

    return true;
}

void Simulation::DoCollisions(const float dt)
{
    std::vector<HitResult> hitResults(m_Transforms.size() * m_Transforms.size());

    for(size_t i = 0; i < m_Transforms.size(); ++i)
    {
        for(size_t j = i + 1; j < m_Transforms.size(); ++j)
        {
            HitResult hitResult;
            if(!SphereSphereSweep(m_Transforms[i],
                   m_Bodies[i],
                   m_Colliders[i],
                   m_Transforms[j],
                   m_Bodies[j],
                   m_Colliders[j],
                   dt,
                   hitResult))
            {
                continue;
            }

            // Compute relative velocity along the normal
            const float vRel =
                (m_Bodies[i].Velocity - m_Bodies[j].Velocity).Dot(hitResult.ContactNormal);

            // Only resolve if bodies are moving towards each other
            if (vRel < 0)
            {
                const float mA = m_Bodies[i].Mass.Value();
                const float mB = m_Bodies[j].Mass.Value();

                // Impulse
                constexpr float e = 0.2f; // Coefficient of restitution
                [[maybe_unused]] const float impulse = -(1 + e) * vRel / (1 / mA + 1 / mB);
                [[maybe_unused]] const float impulse2 = ((mA*mB)/(mA + mB)) * -(1 + e) * vRel;
                const float impulse3 = -(1 + e) * vRel * (mA * mB) / (mA + mB);

                // Reflect velocities along the contact normal.
                m_Bodies[i].Velocity += (impulse3 * hitResult.ContactNormal) / mA;
                m_Bodies[j].Velocity -= (impulse3 * hitResult.ContactNormal) / mB;
            }

            // If overlapping, apply positional correction.
            const float radiusA = std::get<SphereCollider>(m_Colliders[i].Shape).Radius;
            const float radiusB = std::get<SphereCollider>(m_Colliders[j].Shape).Radius;
            constexpr float EPSILON_SQ = 1e-6f;
            constexpr float positionalCorrectionPercent = 0.8f;
            constexpr float correctionSlop = 1e-3f;

            const float minSeparation = radiusA + radiusB;
            const float minSeparationSq = minSeparation * minSeparation;

            const Vec3f deltaPos = m_Transforms[i].T - m_Transforms[j].T;
            const float distSq = deltaPos.Dot(deltaPos);
            if(distSq < minSeparationSq)
            {
                const float mA = m_Bodies[i].Mass.Value();
                const float mB = m_Bodies[j].Mass.Value();
                const float invMA = 1.0f / mA;
                const float invMB = 1.0f / mB;
                const float invMassSum = invMA + invMB;

                const Vec3f correctionNormal =
                    distSq > EPSILON_SQ ? deltaPos / std::sqrtf(distSq) : hitResult.ContactNormal;
                const float dist = distSq > EPSILON_SQ ? std::sqrtf(distSq) : 0.0f;
                const float penetration = minSeparation - dist;
                const float correctionMagnitude =
                    std::max(0.0f, penetration - correctionSlop) * positionalCorrectionPercent;
                const Vec3f correction = correctionMagnitude * correctionNormal / invMassSum;

                m_Transforms[i].T += correction * invMA;
                m_Transforms[j].T -= correction * invMB;
            }
        }
    }
}

static constexpr float G = 0.01f;//6.674e-11f;        // Gravitational constant (m^3 kg^-1 s^-2)

static void ApplyGravity(Simulation& sim)
{
    // Compute gravitational forces between all pairs of bodies.
    for(size_t i = 0; i < sim.m_Bodies.size(); ++i)
    {
        for(size_t j = i + 1; j < sim.m_Bodies.size(); ++j)
        {
            const float radiusA = std::get<SphereCollider>(sim.m_Colliders[i].Shape).Radius;
            const float radiusB = std::get<SphereCollider>(sim.m_Colliders[j].Shape).Radius;
            const float minSeparation = radiusA + radiusB;
            const float minSeparationSq = minSeparation * minSeparation;

            // Vector from body i to body j
            const Vec3f delta = sim.m_Transforms[j].T - sim.m_Transforms[i].T;

            // Gravitational force magnitude: F = G * (M * m) / r^2
            // Direction toward source: delta / r
            // Combined vector form: F = G * M * m * delta / r^3

            // If bodies overlap clamp to minimum separation.
            const float r2 = std::max(delta.Dot(delta), minSeparationSq);
            const float massProduct = sim.m_Bodies[i].Mass.Value() * sim.m_Bodies[j].Mass.Value();
            const Vec3f F = G * massProduct * delta / (r2 * std::sqrtf(r2));

            sim.m_Bodies[i].Force1 += F;
            sim.m_Bodies[j].Force1 -= F;
        }
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
    Simulation simulation;
    EcsRegistry registry;
    WalkMouseNav mouseNav;

    MLG_CHECK(renderer.Startup());
    MLG_CHECK(compositor.Startup());
    MLG_CHECK(imGuiRenderer.Startup());
    MLG_CHECK(textureCache.Startup());

    MLG_CHECK(Load("", textureCache, propKit, level));

    MLG_CHECK(Scene::Create(level, propKit, scene));

    MLG_CHECK(Simulation::Create(level, simulation));

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
                break;
            case SDL_EVENT_KEY_UP:
                if(mouseCaptured)
                {
                    mouseNav.OnKeyUp(event.key.scancode);
                }
                break;
            }
        }

        constexpr float physicsTimeStep = 1.0f/60;//1.0f / PHYSICS_FPS;

        ApplyGravity(simulation);

        simulation.Integrate(physicsTimeStep);

        simulation.DoCollisions(physicsTimeStep);

        simulation.SyncToLevel(level);

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

        RenderGui();

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

static Result<> RenderGui()
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