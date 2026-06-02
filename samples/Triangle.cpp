#include "Compositor.h"
#include "FileFetcher.h"
#include "ImGuiRenderer.h"
#include "Level.h"
#include "Log.h"
#include "PerfMetrics.h"
#include "Projection.h"
#include "PropKit.h"
#include "Renderer.h"
#include "Scene.h"
#include "scope_exit.h"
#include "TextureCache.h"
#include "ThreadPool.h"
#include "WebgpuHelper.h"

#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
#include <thread>

namespace
{
constexpr const char* kAppName = "Triangle";

Result<> RenderGui()
{
    ImGui::SetNextWindowSize(ImVec2(0, 0)); // Auto-fit both width and height
    ImGui::Begin("Timers");

    constexpr size_t kMaxTimers = 256;

    PerfTimerStats timerStats[kMaxTimers];
    std::span<PerfTimerStats> timerStatsSpan(timerStats);
    const size_t timerCount = PerfMetrics::SampleTimers(timerStatsSpan);
    for(const auto& timerStat : timerStatsSpan.first(timerCount))
    {
        const std::string text =
            std::format("{}: {:.3f} ms", timerStat.GetName(), timerStat.GetEMA() * 1000.0f);
        ImGui::Text("%s", text.c_str()); // NOLINT(cppcoreguidelines-pro-type-vararg)
    }

    ImGui::End();

    return Result<>::Ok;
}

Result<> CreateTriangleModel(PropKitDef& outPropKit, LevelDef& outLevelDef)
{
    std::vector<Vertex> triangleVertices = //
        {
            { .pos{ 0.0f, 0.5f, 0.0f }, .normal{ 0.0f, 0.0f, -1.0f }, .uvs{{ .u = 1, .v = 1 }} },  // 0
            { .pos{ 0.5f, 0.0f, 0.0f }, .normal{ 0.0f, 0.0f, -1.0f }, .uvs{{ .u = 0, .v = 1 }} },  // 1
            { .pos{ -0.5f, 0.0f, 0.0f }, .normal{ 0.0f, 0.0f, -1.0f }, .uvs{{ .u = 0, .v = 0 }} }, // 2
        };

    std::vector<VertexIndex> triangleIndices = { 0, 1, 2 };

    MaterialDef mtlDef //
        {
            .BaseTextureUri{ "images/Ant.png" },
            .Color{ "#FFA500"_rgba },
            .Metalness = 0,
            .Roughness = 0,
        };

    MeshDef meshDef //
        {
            .Vertices{ std::move(triangleVertices) },
            .Indices{ std::move(triangleIndices) },
            .MaterialDef{ std::move(mtlDef) },
        };

    ModelDef modelDef //
        {
            .Name{ "Triangle" },
            .MeshDefs{ std::move(meshDef) },
        };

    PropKitDef propKitDef //
        {
            .ModelDefs{ std::move(modelDef) },
        };

    LevelDef levelDef //
        {
            .NodeDefs //
            {
                {
                    .Name{ "TriangleNode" },
                    .Transform{},
                    .Components//
                    {
                        .Model = ModelRef{.Name = "Triangle"},
                    },
                },
            },
        };

    outPropKit = std::move(propKitDef);
    outLevelDef = std::move(levelDef);

    return Result<>::Ok;
}

Result<> MainLoop()
{
    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    MLG_CHECK(ThreadPool::Startup());
    defer
    {
        ThreadPool::Shutdown();
    };

    MLG_CHECK(FileFetcher::Startup());
    defer
    {
        FileFetcher::Shutdown();
    };

    MLG_CHECK(WebgpuHelper::Startup(kAppName));
    defer
    {
        WebgpuHelper::Shutdown();
    };

    TrsTransformf cameraXform;
    cameraXform.T = Vec3f{ 0,0,-4 };
    Projection projection;

    PropKitDef propKitDef;
    LevelDef levelDef;
    MLG_CHECK(CreateTriangleModel(propKitDef, levelDef));

    TextureCache textureCache;
    MLG_CHECK(textureCache.Startup());

    const std::filesystem::path rootPath = ".";
    PropKit propKit;
    Level level;
    Scene scene;
    MLG_CHECK(PropKit::Create(rootPath, textureCache, propKitDef, propKit));
    MLG_CHECK(Level::Create(levelDef, propKit, level));
    MLG_CHECK(Scene::Create(level, propKit, scene));

    Renderer renderer;
    MLG_CHECK(renderer.Startup());

    Compositor compositor;

    ImGuiRenderer imGuiRenderer;
    MLG_CHECK(imGuiRenderer.Startup());

    bool running = true;
    bool minimized = false;

    while(running)
    {
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

            /*case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
            case SDL_EVENT_MOUSE_MOTION:
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
            case SDL_EVENT_MOUSE_BUTTON_UP:
            case SDL_EVENT_MOUSE_WHEEL:
            case SDL_EVENT_KEY_DOWN:
            case SDL_EVENT_KEY_UP:
                break;*/

            default:
                break;
            }
        }

        if(minimized || !running)
        {
            continue;
        }

        auto screenBounds = WebgpuHelper::GetScreenBounds();
        projection.SetAspectRatio(screenBounds.GetAspectRatio());

        compositor.BeginFrame();

        imGuiRenderer.NewFrame();

        RenderGui();

        nonGpuWorkTimer.Stop();

        auto renderResult = renderer.Render(cameraXform,
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

#if !defined(OFFSCREEN_RENDERING) || !OFFSCREEN_RENDERING
        MLG_CHECK(WebgpuHelper::GetSurface().Present(), "Failed to present backbuffer");
#endif

#endif

        WebgpuHelper::GetInstance().ProcessEvents();

        frameTimer.Stop();
    }

    imGuiRenderer.Shutdown();
    renderer.Shutdown();
    textureCache.Shutdown();

    PerfMetrics::LogTimers();

    return Result<>::Ok;
}
} // namespace

int main(int /*argc*/, char** /*argv*/)
{
    MainLoop();

    return 0;
}