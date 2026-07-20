#include "Camera.h"
#include "FileFetcher.h"
#include "GpuHelper.h"
#include "ImGuiRenderer.h"
#include "Level.h"
#include "Log.h"
#include "PerfMetrics.h"
#include "PropKit.h"
#include "Renderer.h"
#include "Scene.h"
#include "ThreadPool.h"

#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <memory>
#include <SDL3/SDL_events.h>
#include <thread>

namespace
{
constexpr const char* kAppName = "Triangle";

Result<>
RenderGui()
{
    ImGui::SetNextWindowSize(ImVec2(0, 0)); // Auto-fit both width and height
    ImGui::Begin("Counters");

    constexpr size_t kMaxPerfStats = 256;

    PerfStats perfStats[kMaxPerfStats];
    std::span<PerfStats> perfStatsSpan(perfStats);
    const size_t counterCount = PerfMetrics::SampleCounters(perfStatsSpan);
    for(const auto& counterStat : perfStatsSpan.first(counterCount))
    {
        const std::string text =
            std::format("{}: {:.3f} ms", counterStat.GetName(), counterStat.GetEMA());
        ImGui::Text("%s", text.c_str()); // NOLINT(cppcoreguidelines-pro-type-vararg)
    }

    ImGui::End();

    return Result<>::Ok;
}

Result<>
CreateTriangleModel(PropKitDef& outPropKitDef, LevelDef& outLevelDef)
{
    std::vector<Vertex> triangleVertices = //
        {
            { .pos{ 0.0f, 0.5f, 0.0f },
                .normal{ 0.0f, 0.0f, -1.0f },
                .uvs{ { .u = 1, .v = 1 } } }, // 0
            { .pos{ 0.5f, 0.0f, 0.0f },
                .normal{ 0.0f, 0.0f, -1.0f },
                .uvs{ { .u = 0, .v = 1 } } }, // 1
            { .pos{ -0.5f, 0.0f, 0.0f },
                .normal{ 0.0f, 0.0f, -1.0f },
                .uvs{ { .u = 0, .v = 0 } } }, // 2
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
                    .Components //
                    {
                        .Model = ModelRef{ .Name = "Triangle" },
                    },
                },
            },
        };

    outPropKitDef = std::move(propKitDef);
    outLevelDef = std::move(levelDef);

    return Result<>::Ok;
}

Result<>
MainLoop()
{
    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    auto task = GpuHelper::Create(kAppName);
    MLG_CHECK(task, "Failed to create GpuHelper");

    while(!task->IsComplete())
    {
        MLG_CHECK(task->Update());
    }

    MLG_CHECK(task->Succeeded(), "System creation failed");
    auto gpuHelperResult = task->Get();
    MLG_CHECK(gpuHelperResult, "Failed to get GpuHelper instance");
    std::unique_ptr<GpuHelper> gpuHelper(std::move(*gpuHelperResult));

    auto threadPoolResult = ThreadPool::Create();
    MLG_CHECK(threadPoolResult, "Failed to create ThreadPool");
    std::unique_ptr<ThreadPool> threadPool(std::move(*threadPoolResult));

    auto fileFetcherResult = FileFetcher::Create();
    MLG_CHECK(fileFetcherResult, "Failed to create FileFetcher");
    std::unique_ptr<FileFetcher> fileFetcher(std::move(*fileFetcherResult));

    auto rendererResult = Renderer::Create(*gpuHelper, *fileFetcher);
    MLG_CHECK(rendererResult, "Failed to create Renderer");
    std::unique_ptr<Renderer> renderer(std::move(*rendererResult));

    auto imGuiRendererResult = ImGuiRenderer::Create(*gpuHelper);
    MLG_CHECK(imGuiRendererResult, "Failed to create ImGuiRenderer");
    std::unique_ptr<ImGuiRenderer> imGuiRenderer(std::move(*imGuiRendererResult));

    PropKitDef propKitDef;
    LevelDef levelDef;
    MLG_CHECK(CreateTriangleModel(propKitDef, levelDef));

    const std::filesystem::path rootPath = ".";
    auto propKitResult =
        PropKit::Create(*gpuHelper, *threadPool, *fileFetcher, rootPath, propKitDef);
    MLG_CHECK(propKitResult, "Failed to create PropKit");
    const PropKit& propKit = *propKitResult;

    auto levelResult = Level::Create(levelDef, propKit);
    MLG_CHECK(levelResult, "Failed to create Level");
    const Level& level = *levelResult;

    auto sceneResult = Scene::Create(*gpuHelper, level);
    MLG_CHECK(sceneResult, "Failed to create Scene");
    const Scene& scene = *sceneResult;

    const TrTransformf cameraXForm{ .T{ 0, 0, -4 } };
    const Viewport viewport(gpuHelper->GetScreenDimensions());
    Camera camera(viewport);

    bool running = true;
    bool minimized = false;

    while(running)
    {
        MLG_SCOPED_TIMER("Frame");

        {
            MLG_SCOPED_TIMER("Non-GPU Work");

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

                switch(event.type)
                {
                    case SDL_EVENT_QUIT:
                        running = false;
                        break;

                    // case SDL_EVENT_WINDOW_RESIZED:
                    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    {
                        const uint32_t newWidth = static_cast<uint32_t>(event.window.data1);
                        const uint32_t newHeight = static_cast<uint32_t>(event.window.data2);
                        MLG_CHECKV(gpuHelper->Resize(newWidth, newHeight));
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
        }

        const Viewport curViewport(gpuHelper->GetScreenDimensions());
        camera.SetViewport(curViewport);

        auto target = gpuHelper->GetSwapChainTexture();

        auto validTarget = GpuValidTexture::Create(target);
        MLG_CHECK(validTarget, "Failed to create valid render target");

        MLG_CHECK(renderer->Render(camera, cameraXForm, scene, propKit));
        MLG_CHECK(renderer->Composite(*validTarget));

        MLG_CHECK(imGuiRenderer->Render(gpuHelper->GetDevice(), *validTarget, RenderGui));

#if !defined(__EMSCRIPTEN__)

#if !defined(OFFSCREEN_RENDERING) || !OFFSCREEN_RENDERING
        MLG_CHECK(gpuHelper->GetSurface().Present(), "Failed to present backbuffer");
#endif

#endif

        gpuHelper->GetInstance().ProcessEvents();
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