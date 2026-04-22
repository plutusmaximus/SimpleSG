#include "Compositor.h"
#include "ImGuiRenderer.h"
#include "Log.h"
#include "PerfMetrics.h"
#include "Projection.h"
#include "PropKit.h"
#include "Renderer.h"
#include "Scene.h"
#include "scope_exit.h"
#include "Stopwatch.h"
#include "WebgpuHelper.h"

#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
#include <thread>

static Result<> CreateTriangleModel(PropKitDef& outPropKit, SceneDef& outSceneDef);

constexpr const char* kAppName = "Triangle";

static Result<> RenderGui();

static Result<> MainLoop()
{
    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    MLG_CHECK(WebgpuHelper::Startup(kAppName));

    auto shutdown = scope_exit([]()
    {
        WebgpuHelper::Shutdown();
    });

    auto screenBounds = WebgpuHelper::GetScreenBounds();

    constexpr Radiansf fov = Radiansf::FromDegrees(45);

    TrsTransformf cameraXform;
    cameraXform.T = Vec3f{ 0,0,-4 };
    Projection projection;
    projection.SetPerspective(fov, screenBounds, 0.1f, 1000);

    PropKitDef propKitDef;
    SceneDef sceneDef;
    MLG_CHECK(CreateTriangleModel(propKitDef, sceneDef));

    TextureCache textureCache;
    MLG_CHECK(textureCache.Startup());

    std::filesystem::path rootPath = ".";
    PropKit propKit;
    Scene scene;
    MLG_CHECK(PropKit::Create(rootPath, textureCache, propKitDef, propKit));
    MLG_CHECK(Scene::Create(sceneDef, propKit, scene));

    Renderer renderer;
    MLG_CHECK(renderer.Startup());

    Compositor compositor;
    MLG_CHECK(compositor.Startup());

    ImGuiRenderer imGuiRenderer;
    MLG_CHECK(imGuiRenderer.Startup());

    Stopwatch stopwatch;

    bool running = true;
    bool minimized = false;

    while(running)
    {
        PerfMetrics::BeginFrame();

        static PerfTimer frameTimer("Frame");
        frameTimer.Start();

        static PerfTimer nonGpuWorkTimer("Non-GPU Work");
        nonGpuWorkTimer.Start();

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
            ImGui_ImplSDL3_ProcessEvent(&event);

            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            //case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                WebgpuHelper::Resize(event.window.data1, event.window.data2);
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                minimized = true;
                break;

            //case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                //app->OnFocusGained();
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                //app->OnFocusLost();
                break;

            case SDL_EVENT_MOUSE_MOTION:
                //app->OnMouseMove(Vec2f(event.motion.xrel, event.motion.yrel));
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                //app->OnMouseDown(Point(event.button.x, event.button.y), event.button.button - 1);
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                //app->OnMouseUp(event.button.button - 1);
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                //app->OnScroll(Vec2f(event.wheel.x, event.wheel.y));
                break;

            case SDL_EVENT_KEY_DOWN:
                //app->OnKeyDown(event.key.scancode);
                break;

            case SDL_EVENT_KEY_UP:
                //app->OnKeyUp(event.key.scancode);
                break;
            }
        }

        if(minimized || !running)
        {
            continue;
        }

        screenBounds = WebgpuHelper::GetScreenBounds();

        projection.SetBounds(screenBounds);

        TrsTransformf transform;

        compositor.BeginFrame();

        imGuiRenderer.NewFrame();

        RenderGui();

        nonGpuWorkTimer.Stop();

        auto renderResult = renderer.Render(cameraXform.ToMatrix(),
            projection,
            scene,
            propKit,
            compositor);

        MLG_CHECK(renderResult);

        auto imGuiRenderResult = imGuiRenderer.Render(compositor);
        MLG_CHECK(imGuiRenderResult);

        auto endFrameResult = compositor.EndFrame();
        MLG_CHECK(endFrameResult);

#if !defined(__EMSCRIPTEN__)

#if !OFFSCREEN_RENDERING
        MLG_CHECK(WebgpuHelper::GetSurface().Present(), "Failed to present backbuffer");
#endif

#endif

        WebgpuHelper::GetInstance().ProcessEvents();

        frameTimer.Stop();

        PerfMetrics::EndFrame();
    }

    imGuiRenderer.Shutdown();
    compositor.Shutdown();
    renderer.Shutdown();
    textureCache.Shutdown();

    PerfMetrics::LogTimers();

    return Result<>::Ok;
}

int main(int, char* /*argv[]*/)
{
    MainLoop();

    return 0;
}

static Result<> RenderGui()
{
    ImGui::Begin("Timers");
    PerfMetrics::TimerStat timers[256];
    unsigned timerCount = PerfMetrics::GetTimers(timers, std::size(timers));
    for(unsigned i = 0; i < timerCount; ++i)
    {
        ImGui::Text("%s: %.3f ms", timers[i].GetName().c_str(), timers[i].GetValue() * 1000.0f);
    }
    ImGui::End();

    return Result<>::Ok;
}

static Result<> CreateTriangleModel(PropKitDef& outPropKit, SceneDef& outSceneDef)
{
    std::vector<Vertex> triangleVertices = //
        {
            { { 0.0f, 0.5f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 1, 1 } },  // 0
            { { 0.5f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0, 1 } },  // 1
            { { -0.5f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, { 0, 0 } }, // 2
        };

    std::vector<VertexIndex> triangleIndices = //
    {
        0, 1, 2,
    };

    const MaterialDef mtlDef //
    {
        .BaseTextureUri = "images/Ant.png",
        .Color = {"#FFA500"_rgba},
        .Metalness = 0,
        .Roughness = 0
    };

    MeshDef meshDef//
    {
        .Vertices = std::move(triangleVertices),
        .Indices = std::move(triangleIndices),
        .MaterialDef = std::move(mtlDef),
    };

    ModelDef modelDef//
    {
        .MeshDefs = { std::move(meshDef) },
    };

    const TransformDef transformDef //
        {
            .Transform = Mat44f(1),
            .ParentIndex = TransformDef::kInvalidParentIndex,
        };

    const ModelInstance modelInstance //
    {
        .ModelIndex = 0,
        .TransformIndex = 0,
    };

    PropKitDef propKitDef({ std::move(modelDef) }, { { "triangle", 0 } });

    SceneDef sceneDef //
        {
            .TransformDefs = { std::move(transformDef) },
            .ModelInstances = { { 0, 0 } },
        };

    outPropKit = std::move(propKitDef);
    outSceneDef = std::move(sceneDef);

    return Result<>::Ok;
}