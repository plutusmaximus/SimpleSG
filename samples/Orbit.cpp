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
    Level& outLevel,
    Scene& outScene)
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
    constexpr float MAX_RADIUS = 0.5f;//1.0f;
    constexpr float MIN_RADIUS = 0.5f;//0.1f;
    constexpr float MAX_SPEED = 0.5f;
    constexpr float MIN_SPEED = 0.1f;

    std::vector<LevelNodeDef> nodeDefs;
    nodeDefs.reserve(1000);
    for(size_t i = 0; i < nodeDefs.capacity(); ++i)
    {
        const float radius = MIN_RADIUS + std::abs(dis(gen)) * (MAX_RADIUS - MIN_RADIUS);
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
                .Body = RigidBodyDef{ .Velocity{ velocity }, .Mass{ radius } },
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

    MLG_CHECK(Scene::Create(outLevel, outPropKit, outScene),
        "Failed to create Scene for {}",
        path.string());

    return Result<>::Ok;
}

struct Simulation
{
    std::vector<TrsTransformf> Transforms;
    std::vector<RigidBody> Bodies;
};

struct HitResult
{
    float TimeOfImpact;
    Vec3f ContactPoint;
    Vec3f ContactNormal;
};

static bool
SphereSphereSweep(const TrsTransformf& tA,
    const RigidBody& bodyA,
    const TrsTransformf& tB,
    const RigidBody& bodyB,
    const float deltaTime,
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

    const float radiusA = 0.5f;
    const float radiusB = 0.5f;

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

    if (t < 0.0f || t > deltaTime)
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

static void DoCollisions(Simulation& sim, const float deltaTime)
{
    std::vector<HitResult> hitResults(sim.Transforms.size() * sim.Transforms.size());

    for(size_t i = 0; i < sim.Transforms.size(); ++i)
    {
        for(size_t j = i + 1; j < sim.Transforms.size(); ++j)
        {
            HitResult hitResult;
            if(!SphereSphereSweep(sim.Transforms[i],
                   sim.Bodies[i],
                   sim.Transforms[j],
                   sim.Bodies[j],
                   deltaTime,
                   hitResult))
            {
                continue;
            }

            // Simple collision response: reflect velocities along the contact normal.
            // This is not physically accurate, but it demonstrates the concept.

            const Vec3f n = hitResult.ContactNormal;

            Vec3f vA = sim.Bodies[i].Velocity;
            Vec3f vB = sim.Bodies[j].Velocity;

            // Compute relative velocity along the normal
            const float vRel = (vA - vB).Dot(n);

            // Only resolve if bodies are moving towards each other
            if (vRel < 0)
            {
                const float mA = sim.Bodies[i].Mass.Value();
                const float mB = sim.Bodies[j].Mass.Value();

                // Compute impulse scalar
                const float e = 0.2f; // Coefficient of restitution (1.0 for perfectly elastic)
                const float impulse = -(1 + e) * vRel / (1 / mA + 1 / mB);

                // Apply impulse to each body's velocity
                sim.Bodies[i].Velocity += (impulse * n) / mA;
                sim.Bodies[j].Velocity -= (impulse * n) / mB;
            }
        }
    }
}

static constexpr float G = 0.01f;//6.674e-11f;        // Gravitational constant (m^3 kg^-1 s^-2)

static void VerletOrbit(Simulation& sim, float deltaTime)
{
    std::vector<Vec3f> oldForce;
    oldForce.reserve(sim.Bodies.size());

    // Update positions based on current velocities and forces.
    for (size_t i = 0; i < sim.Bodies.size(); ++i)
    {
        RigidBody& b = sim.Bodies[i];
        TrsTransformf& trs = sim.Transforms[i];
        const Vec3f acceleration = b.Force / b.Mass.Value();
        trs.T += b.Velocity * deltaTime + 0.5f * acceleration * deltaTime * deltaTime;
        oldForce.emplace_back(b.Force);
        b.Force = Vec3f{ 0 };
    }

    // Softening factor to prevent singularities and extreme forces at very close distances.
    constexpr float softeningSquared = 0.01f;

    // Calculate gravitational forces between all pairs of bodies.
    for(size_t i = 0; i < sim.Bodies.size(); ++i)
    {
        for(size_t j = i + 1; j < sim.Bodies.size(); ++j)
        {
            // Vector from body i to body j
            const Vec3f delta = sim.Transforms[j].T - sim.Transforms[i].T;

            // Gravitational force magnitude: F = G * (M * m) / r^2
            // Direction toward source: delta / r
            // Combined vector form: F = G * M * m * delta / r^3

            //Distance squared with softening to prevent singularity
            const float r2 = delta.Dot(delta) + softeningSquared;
            const float invR = 1.0f / std::sqrtf(r2);
            const float invR3 = (1.0f / r2) * invR;
            const float massProduct = sim.Bodies[i].Mass.Value() * sim.Bodies[j].Mass.Value();
            const Vec3f force = G * massProduct * invR3 * delta;

            sim.Bodies[i].Force += force;
            sim.Bodies[j].Force -= force;
        }
    }

    // Update velocities based on new forces.
    for (size_t i = 0; i < sim.Bodies.size(); ++i)
    {
        RigidBody& b = sim.Bodies[i];
        const Vec3f oldAcceleration = oldForce[i] / b.Mass.Value();
        const Vec3f acceleration = b.Force / b.Mass.Value();
        b.Velocity += 0.5f * (acceleration + oldAcceleration) * deltaTime;
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
    EcsRegistry registry;
    WalkMouseNav mouseNav;

    MLG_CHECK(renderer.Startup());
    MLG_CHECK(compositor.Startup());
    MLG_CHECK(imGuiRenderer.Startup());
    MLG_CHECK(textureCache.Startup());

    MLG_CHECK(Load("", textureCache, propKit, level, scene));

    auto allHandles = level.GetAllHandles();
    std::vector<TrsTransformf> transforms;
    std::vector<RigidBody> bodies;
    std::unordered_map<std::string_view, size_t> bodyNameToIndex;
    transforms.reserve(allHandles.size());
    bodies.reserve(allHandles.size());
    bodyNameToIndex.reserve(allHandles.size());
    for(const auto& handle : allHandles)
    {
        const auto& node = level.GetNode(handle);
        if(node->Components.Body)
        {
            bodyNameToIndex[node->Name] = transforms.size();
            transforms.emplace_back(node->LocalTransform);
            bodies.emplace_back(*node->Components.Body);
        }
    }

    Simulation simulation//
    {
        .Transforms{std::move(transforms)},
        .Bodies{std::move(bodies)},
    };

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

        constexpr float physicsTimeStep = 1.0f/30;//1.0f / PHYSICS_FPS;

        VerletOrbit(simulation, physicsTimeStep);

        DoCollisions(simulation, physicsTimeStep);

        for(const auto& foo : bodyNameToIndex)
        {
            const std::string_view name = foo.first;
            const size_t index = foo.second;
            const TrsTransformf& xform = simulation.Transforms[index];
            auto nodeHandle = level.GetNodeHandle({ name });
            MLG_CHECK(nodeHandle, "Failed to find node with name {}", name);
            MLG_CHECK(level.UpdateLocalTransform(*nodeHandle, xform));
            MLG_CHECK(scene.UpdateWorldTransform(*nodeHandle, level.GetNode(*nodeHandle)->WorldTransform));
        }

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