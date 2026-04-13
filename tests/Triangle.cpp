#include "Camera.h"
#include "DawnRenderCompositor.h"
#include "DawnRenderer.h"
#include "DawnSceneKit.h"
#include "ImGuiRenderer.h"
#include "Log.h"
#include "PerfMetrics.h"
#include "scope_exit.h"
#include "Stopwatch.h"
#include "WebgpuHelper.h"

#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
#include <thread>

static Result<SceneKitSourceData> CreateTriangleModel();

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
    Camera camera;
    camera.SetPerspective(fov, screenBounds, 0.1f, 1000);

    auto sceneKitData = CreateTriangleModel();
    MLG_CHECK(sceneKitData);

    wgpu::Device wgpuDevice = WebgpuHelper::GetDevice();
    std::filesystem::path rootPath = ".";
    auto dawnSceneKit = DawnSceneKit::Create(wgpuDevice, rootPath, *sceneKitData);
    MLG_CHECK(dawnSceneKit);

    SceneKit* sceneKit = *dawnSceneKit;

    auto rendererResult = DawnRenderer::Create(WebgpuHelper::GetWindow(),
        WebgpuHelper::GetDevice(),
        WebgpuHelper::GetSurface());
    MLG_CHECK(rendererResult);

    auto renderCompositorResult = DawnRenderCompositor::Create();
    MLG_CHECK(renderCompositorResult);

    auto imGuiRendererResult = ImGuiRenderer::Create();
    MLG_CHECK(imGuiRendererResult);

    DawnRenderer* renderer = *rendererResult;
    DawnRenderCompositor* renderCompositor = *renderCompositorResult;
    ImGuiRenderer* imGuiRenderer = *imGuiRendererResult;

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

        camera.SetBounds(screenBounds);

        TrsTransformf transform;

        renderCompositor->BeginFrame();

        imGuiRenderer->NewFrame();

        RenderGui();

        nonGpuWorkTimer.Stop();

        auto renderResult = renderer->Render(cameraXform.ToMatrix(),
            camera.GetProjection(),
            *sceneKit,
            renderCompositor);

        MLG_CHECK(renderResult);

        auto imGuiRenderResult = imGuiRenderer->Render(renderCompositor);
        MLG_CHECK(imGuiRenderResult);

        auto endFrameResult = renderCompositor->EndFrame();
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

    ImGuiRenderer::Destroy(imGuiRenderer);
    DawnRenderCompositor::Destroy(renderCompositor);
    DawnRenderer::Destroy(renderer);

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

static Result<SceneKitSourceData> CreateTriangleModel()
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

    const MaterialData mtlData //
    {
        .BaseTextureUri = "images/Ant.png",
        .Color = {"#FFA500"_rgba},
        .Metalness = 0,
        .Roughness = 0
    };

    const TransformData transformData //
        {
            .Transform = Mat44f(1),
            .ParentIndex = TransformData::kInvalidParentIndex,
        };

    const MeshData meshData //
    {
        .FirstIndex = 0,
        .IndexCount = static_cast<uint32_t>(triangleIndices.size()),
        .BaseVertex = 0,
        .MaterialIndex = 0,
    };

    const ModelInstance modelInstance //
    {
        .FirstMesh = 0,
        .MeshCount = 1,
        .TransformIndex = 0,
    };

    SceneKitSourceData sceneKitData;

    sceneKitData.Vertices = std::move(triangleVertices);
    sceneKitData.Indices = std::move(triangleIndices);
    sceneKitData.Materials.emplace_back(std::move(mtlData));
    sceneKitData.Transforms.emplace_back(std::move(transformData));
    sceneKitData.Meshes.emplace_back(std::move(meshData));
    sceneKitData.ModelInstances.emplace_back(std::move(modelInstance));

    return std::move(sceneKitData);
}