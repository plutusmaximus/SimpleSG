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

    MeshDef meshDef //
        {
            .Vertices{ shape.GetVertices().begin(), shape.GetVertices().end() },
            .Indices{ shape.GetIndices().begin(), shape.GetIndices().end() },
        };

    ModelDef modelDef //
        {
            .Name{ "Shape" },
            .MeshDefs{ meshDef },
        };

    PropKitDef propKitDef //
        {
            .ModelDefs{ std::move(modelDef) },
        };

    MLG_CHECK(PropKit::Create(path.parent_path(), textureCache, propKitDef, outPropKit),
        "Failed to create PropKit for {}",
        path.string());

    LevelDef levelDef //
        {
            .NodeDefs //
            {
                {
                    .Name{ "Orbit" },
                    .Transform{},
                    .ModelName{ "Shape" },
                    .Children //
                    {
                        {
                            .Name{ "MoonOrbit" },
                            .Transform{},
                            .Children //
                            {
                                {
                                    .Name{ "Moon" },
                                    .Transform{ .T{ 2, 0, 0 }, .S{ 0.1f, 0.1f, 0.1f } },
                                    .ModelName{ "Shape" },
                                },
                            },
                        },
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

    auto moonOrbit = level.GetNodeHandle({"Orbit", "MoonOrbit"});
    MLG_CHECK(moonOrbit);

    auto moon = level.GetNodeHandle({"Orbit", "MoonOrbit", "Moon"});
    MLG_CHECK(moon);

    Entity model = registry.CreateEntity(TrsTransformf{}, WorldMatrix{}, ModelTag{});

    Entity camera = registry.CreateEntity(TrsTransformf{.T{0,0,-4}}, WorldMatrix{}, Projection{});

    mouseNav.SetTransform(camera.Get<TrsTransformf>());

    uint64_t frameBeginTicks = SDL_GetTicksNS();

    bool mouseCaptured = true;
    SDL_SetWindowRelativeMouseMode(WebgpuHelper::GetWindow(), mouseCaptured);

    Radiansf orbitAngle{0};

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

        {
            TrsTransform trs = level.GetNode(*moonOrbit)->LocalTransform;
            orbitAngle += Radiansf(0.01f);
            trs.R = Quatf(orbitAngle, Vec3f::YAXIS());
            MLG_CHECK(level.UpdateLocalTransform(*moonOrbit, trs));

            MLG_CHECK(scene.UpdateWorldTransform(*moon, level.GetNode(*moon)->WorldTransform));
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