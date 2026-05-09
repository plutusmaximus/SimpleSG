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

    std::vector<LevelNodeDef> nodeDefs;
    nodeDefs.reserve(1000);
    for(size_t i = 0; i < nodeDefs.capacity(); ++i)
    {
        const float radius = 0.5f + std::abs(dis(gen));

        LevelNodeDef nodeDef//
        {
            .Name{ std::format("Body{}", i) },
            .Transform{ .T{ dis(gen) * 20, dis(gen) * 20, dis(gen) * 20 }, .S{ radius } },
            .Components //
            {
                .Model = ModelRef{ .Name = "Shape" },
                .Body = RigidBodyDef{ .Velocity = Vec3f{ dis(gen), dis(gen), dis(gen) } * 0.5f, .Mass{ radius } },
                .Collider = ColliderDef{ SphereDef{ .Radius = radius } },
            },
        };
        nodeDefs.emplace_back(std::move(nodeDef));
    }

    LevelDef levelDef //
    {
        .NodeDefs = std::move(nodeDefs),
    };

    /*LevelDef levelDef //
        {
            .NodeDefs //
            {
                {
                    .Name{ "Planet" },
                    .Transform{},
                    .Components //
                    {
                        .Model = ModelRef{ .Name = "Shape" },
                        .Body = RigidBodyDef{ .Velocity{ 0 }, .Mass{2} },
                        .Collider = ColliderDef{ SphereDef{ .Radius = 1 } },
                    },
                },
                {
                    .Name{ "Moon1" },
                    .Transform{ .T{ -2, 0, 0 }, .S{ 0.5f } },
                    .Components //
                    {
                        .Model = ModelRef{ .Name = "Shape" },
                        .Body = RigidBodyDef{ .Velocity{ 0 }, .Mass{1} },
                        .Collider = ColliderDef{ SphereDef{ .Radius = 0.5f } },
                    },
                },
                {
                    .Name{ "Moon2" },
                    .Transform{ .T{ 0, 2, 0 }, .S{ 0.5f } },
                    .Components //
                    {
                        .Model = ModelRef{ .Name = "Shape" },
                        .Body = RigidBodyDef{ .Velocity{ 0 }, .Mass{1} },
                        .Collider = ColliderDef{ SphereDef{ .Radius = 0.5f } },
                    },
                },
            },
        };*/

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

static constexpr float G = 0.01f;//6.674e-11f;        // Gravitational constant (m^3 kg^-1 s^-2)
/*[[maybe_unused]]static void VerletOrbit(RigidBody& body, const RigidBody& centerBody, float deltaTime)
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
}*/

static void VerletOrbit(Simulation& sim, float deltaTime)
{
    std::vector<Vec3f> accelerations(sim.Bodies.size(), Vec3f{ 0 });
    constexpr float softeningSquared = 0.01f; // Softening factor to prevent singularities and extreme forces at very close distances.

    for(size_t i = 0; i < sim.Bodies.size(); ++i)
    {
        for(size_t j = i + 1; j < sim.Bodies.size(); ++j)
        {
            Vec3f delta = sim.Transforms[j].T - sim.Transforms[i].T;

            // Gravitational force magnitude: F = G * (M * m) / r^2
            // Acceleration: a = F / m
            float r2 = delta.Dot(delta) + softeningSquared;
            float invR = static_cast<float>(1.0 / std::sqrt(r2));
            float invR3 = invR * invR * invR;

            Vec3f accelI = (G * sim.Bodies[j].Mass * invR3).Value() * delta;
            Vec3f accelJ = (-G * sim.Bodies[i].Mass * invR3).Value() * delta;

            accelerations[i] += accelI;
            accelerations[j] += accelJ;
        }
    }

    for (size_t i = 0; i < sim.Bodies.size(); ++i)
    {
        RigidBody& b = sim.Bodies[i];
        TrsTransformf& trs = sim.Transforms[i];
        trs.T += b.Velocity * deltaTime + 0.5f * accelerations[i] * deltaTime * deltaTime;
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

        VerletOrbit(simulation, elapsedSeconds);

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