#include "Camera.h"
#include "DawnSceneKit.h"
#include "DawnGpuDevice.h"
#include "FileIo.h"
#include "ImGuiRenderer.h"
#include "Log.h"
#include "PerfMetrics.h"
#include "SdlGpuDevice.h"
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

    MLG_CHECK(FileIo::Startup(), "Failed to startup File I/O system");

    auto fileIoCleanup = scope_exit([]()
    {
        FileIo::Shutdown();
    });

#if DAWN_GPU
    auto gdResult = DawnGpuDevice::Create();
#else
    auto gdResult = SdlGpuDevice::Create(window);
#endif

    MLG_CHECK(gdResult);

    auto gpuDevice = *gdResult;

    auto gpuDeviceCleanup = scope_exit([gpuDevice]()
    {
#if DAWN_GPU
        DawnGpuDevice::Destroy(gpuDevice);
#else
        SdlGpuDevice::Destroy(gpuDevice);
#endif
    });

    auto screenBounds = gpuDevice->GetScreenBounds();

    constexpr Radiansf fov = Radiansf::FromDegrees(45);

    TrsTransformf cameraXform;
    cameraXform.T = Vec3f{ 0,0,-4 };
    Camera camera;
    camera.SetPerspective(fov, screenBounds, 0.1f, 1000);

    auto sceneKitData = CreateTriangleModel();
    MLG_CHECK(sceneKitData);

    wgpu::Device wgpuDevice = static_cast<DawnGpuDevice*>(gpuDevice)->Device;
    std::filesystem::path rootPath = ".";
    auto dawnSceneKit = DawnSceneKit::Create(wgpuDevice, rootPath, *sceneKitData);
    MLG_CHECK(dawnSceneKit);

    SceneKit* sceneKit = *dawnSceneKit;

    Renderer* renderer = gpuDevice->GetRenderer();
    RenderCompositor* renderCompositor = gpuDevice->GetRenderCompositor();
    ImGuiRenderer imGuiRenderer(gpuDevice);

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

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
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

        screenBounds = gpuDevice->GetScreenBounds();

        camera.SetBounds(screenBounds);

        TrsTransformf transform;

        renderCompositor->BeginFrame();

        imGuiRenderer.NewFrame();

        RenderGui();

        nonGpuWorkTimer.Stop();

        auto renderResult = renderer->Render(cameraXform.ToMatrix(),
            camera.GetProjection(),
            *sceneKit,
            renderCompositor);

        MLG_CHECK(renderResult);

        auto imGuiRenderResult = imGuiRenderer.Render(renderCompositor);
        MLG_CHECK(imGuiRenderResult);

        auto endFrameResult = renderCompositor->EndFrame();
        MLG_CHECK(endFrameResult);

#if DAWN_GPU
        auto dawnGpuDevice = static_cast<DawnGpuDevice*>(gpuDevice);

#if !defined(__EMSCRIPTEN__)

#if !OFFSCREEN_RENDERING
        MLG_CHECK(dawnGpuDevice->Surface.Present(), "Failed to present backbuffer");
#endif

#endif

        dawnGpuDevice->Instance.ProcessEvents();
#endif  //DAWN_GPU

        frameTimer.Stop();

        PerfMetrics::EndFrame();
    }

    PerfMetrics::LogTimers();

    return Result<>::Ok;
}

int main(int, char* /*argv[]*/)
{
    MainLoop();

    return 0;
}

static bool show_demo_window = true;
static bool show_another_window = false;
static ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

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

// Triangle vertices
static const Vertex triangleVertices[] = //
    {
        {{0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, -1.0f}, {1, 1}},  // 0
        {{0.5f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0, 1}},  // 1
        {{-0.5f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, {0, 0}}, // 2
};

// Triangle indices
static const VertexIndex triangleIndices[] =
{
    0, 1, 2,
};

static Result<SceneKitSourceData> CreateTriangleModel()
{
    SceneKitSourceData sceneKitData;

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
        .IndexCount = static_cast<uint32_t>(std::size(triangleIndices)),
        .BaseVertex = 0,
        .MaterialIndex = 0,
    };

    const ModelInstance modelInstance //
    {
        .FirstMesh = 0,
        .MeshCount = 1,
        .TransformIndex = 0,
    };

    sceneKitData.Vertices.assign(triangleVertices, triangleVertices + std::size(triangleVertices));
    sceneKitData.Indices.assign(triangleIndices, triangleIndices + std::size(triangleIndices));
    sceneKitData.Materials.emplace_back(mtlData);
    sceneKitData.Transforms.emplace_back(transformData);
    sceneKitData.Meshes.emplace_back(meshData);
    sceneKitData.ModelInstances.emplace_back(modelInstance);

    return std::move(sceneKitData);
}