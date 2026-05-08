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
            .ModelDefs//
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
                }
            }
        };

    MLG_CHECK(PropKit::Create(path.parent_path(), textureCache, propKitDef, outPropKit),
        "Failed to create PropKit for {}",
        path.string());

    LevelDef levelDef //
        {
            .NodeDefs //
            {
                {
                    .Name{ "Planet" },
                    .Transform{},
                    .Components //
                    {
                        .Model = ModelRef{ .Name = "Shape" },
                        .Body = RigidBodyDef{ .Velocity{ 0 }, .Mass = 2 },
                        .Collider = ColliderDef{ SphereDef{ .Radius = 1 } },
                    },
                },
                {
                    .Name{ "Moon1" },
                    .Transform{ .T{ -2, 0, 0 }, .S{ 0.5f } },
                    .Components //
                    {
                        .Model = ModelRef{ .Name = "Shape" },
                        .Body = RigidBodyDef{ .Velocity{ 0 }, .Mass = 1 },
                        .Collider = ColliderDef{ SphereDef{ .Radius = 0.5f } },
                    },
                },
                {
                    .Name{ "Moon2" },
                    .Transform{ .T{ 0, 2, 0 }, .S{ 0.5f } },
                    .Components //
                    {
                        .Model = ModelRef{ .Name = "Shape" },
                        .Body = RigidBodyDef{ .Velocity{ 0 }, .Mass = 1 },
                        .Collider = ColliderDef{ SphereDef{ .Radius = 0.5f } },
                    },
                },
            },
        };

    MLG_CHECK(Level::Create(levelDef, outPropKit, outLevel),
        "Failed to create Level for {}",
        path.string());

    MLG_CHECK(Scene::Create(outLevel, outPropKit, outScene),
        "Failed to create Scene for {}",
        path.string());

    return Result<>::Ok;
}

struct RigidBody
{
    Vec3f Position;
    Vec3f Velocity;
    float Mass;
};

static constexpr float G = 0.01f;//6.674e-11f;        // Gravitational constant (m^3 kg^-1 s^-2)
[[maybe_unused]]static void VerletOrbit(RigidBody& body, const RigidBody& centerBody, float deltaTime)
{
    //static constexpr float CENTRAL_MASS = 10;//1.989e30f; // Mass of central body (kg, ~solar mass)

    const Vec3f toCenter = centerBody.Position - body.Position;
    const float distance = toCenter.Length();
    const Vec3f direction = toCenter / distance;

    // Gravitational force magnitude: F = G * (M * m) / r^2
    const float forceMagnitude = G * centerBody.Mass * body.Mass / (distance * distance);

    // Acceleration: a = F / m
    const Vec3f acceleration = direction * (forceMagnitude / body.Mass);

    // Verlet integration
    body.Position += body.Velocity * deltaTime + acceleration * (deltaTime * deltaTime * 0.5f);
    body.Velocity += acceleration * deltaTime;
}

static void VerletOrbit(std::span<RigidBody*> bodies, float deltaTime)
{
    std::vector<Vec3f> accelerations(bodies.size(), Vec3f{ 0 });
    const float softeningSquared = 0.01f; // Softening factor to prevent singularities and extreme forces at very close distances.

    for(size_t i = 0; i < bodies.size(); ++i)
    {
        for(size_t j = i + 1; j < bodies.size(); ++j)
        {
            Vec3f delta = bodies[j]->Position - bodies[i]->Position;

            float r2 = delta.Dot(delta) + softeningSquared;
            float invR = static_cast<float>(1.0 / std::sqrt(r2));
            float invR3 = invR * invR * invR;

            Vec3f accelI = G * bodies[j]->Mass * delta * invR3;
            Vec3f accelJ = -G * bodies[i]->Mass * delta * invR3;

            accelerations[i] += accelI;
            accelerations[j] += accelJ;
        }
    }

    for (size_t i = 0; i < bodies.size(); ++i)
    {
        RigidBody& b = *bodies[i];
        b.Position += b.Velocity * deltaTime + 0.5f * accelerations[i] * deltaTime * deltaTime;
        b.Velocity += accelerations[i] * deltaTime;
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

    auto planet = level.GetNodeHandle({"Planet"});
    MLG_CHECK(planet);

    auto moon1 = level.GetNodeHandle({"Moon1"});
    MLG_CHECK(moon1);

    auto moon2 = level.GetNodeHandle({"Moon2"});
    MLG_CHECK(moon2);

    Entity model = registry.CreateEntity(TrsTransformf{}, WorldMatrix{}, ModelTag{});

    Entity camera = registry.CreateEntity(TrsTransformf{.T{0,0,-4}}, WorldMatrix{}, Projection{});

    mouseNav.SetTransform(camera.Get<TrsTransformf>());

    uint64_t frameBeginTicks = SDL_GetTicksNS();

    bool mouseCaptured = true;
    SDL_SetWindowRelativeMouseMode(WebgpuHelper::GetWindow(), mouseCaptured);

    RigidBody moon1Body //
        {
            .Position{ -2, 0, 0 },
            .Mass{ 1 },
        };

    RigidBody moon2Body //
        {
            .Position{ 0, 2, 0 },
            .Mass{ 1 },
        };

    RigidBody planetBody //
        {
            .Position{ 0, 0, 0 },
            .Mass{ 2 },
        };

    const float orbitalRadius1 = (moon1Body.Position - planetBody.Position).Length();
    const float orbitalRadius2 = (moon2Body.Position - planetBody.Position).Length();
    const float initialSpeed1 = std::sqrt(G * (planetBody.Mass + moon1Body.Mass) / orbitalRadius1);
    const float initialSpeed2 = std::sqrt(G * (planetBody.Mass + moon2Body.Mass) / orbitalRadius2);
    const float moonInitialSpeed1 = initialSpeed1 * planetBody.Mass / (planetBody.Mass + moon1Body.Mass);
    const float moonInitialSpeed2 = initialSpeed2 * planetBody.Mass / (planetBody.Mass + moon2Body.Mass);

    moon1Body.Velocity = Vec3f{ 0, moonInitialSpeed1, 0 };
    moon2Body.Velocity = Vec3f{ moonInitialSpeed2, 0, 0 };

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

        RigidBody* bodies[] = { &moon1Body, &moon2Body, &planetBody };
        VerletOrbit(bodies, 0.01f);

        {
            TrsTransform trs = level.GetNode(*planet)->LocalTransform;
            trs.T = planetBody.Position;
            MLG_CHECK(level.UpdateLocalTransform(*planet, trs));
            trs = level.GetNode(*moon1)->LocalTransform;
            trs.T = moon1Body.Position;
            MLG_CHECK(level.UpdateLocalTransform(*moon1, trs));
            trs = level.GetNode(*moon2)->LocalTransform;
            trs.T = moon2Body.Position;
            MLG_CHECK(level.UpdateLocalTransform(*moon2, trs));

            MLG_CHECK(scene.UpdateWorldTransform(*planet, level.GetNode(*planet)->WorldTransform));
            MLG_CHECK(scene.UpdateWorldTransform(*moon1, level.GetNode(*moon1)->WorldTransform));
            MLG_CHECK(scene.UpdateWorldTransform(*moon2, level.GetNode(*moon2)->WorldTransform));
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