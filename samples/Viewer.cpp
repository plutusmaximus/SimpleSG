#include "GltfLoader.h"
#include "GpuColorPass.h"
#include "GpuHelper.h"
#include "ImGuiRenderer.h"
#include "InputMapper.h"
#include "Level.h"
#include "MouseNav.h"
#include "PerfMetrics.h"
#include "Camera.h"
#include "PropKit.h"
#include "Renderer.h"
#include "Scene.h"
#include "System.h"
#include "VecMath.h"

#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL_events.h>
#include <thread>

namespace
{
constexpr const char* APP_NAME = "Viewer";

Result<> RenderGui()
{
#if defined (NDEBUG)
    constexpr const char* buildType = "Release";
#else
    constexpr const char* buildType = "Debug";
#endif

    constexpr const char* backend = "Dawn";

    auto title = std::format("Counters: {}/{}", buildType, backend);

    ImGui::SetNextWindowSize(ImVec2(0, 0)); // Auto-fit both width and height
    ImGui::Begin(title.c_str());

    constexpr size_t kMaxPerfStats = 256;

    PerfStats perfStats[kMaxPerfStats];
    std::span<PerfStats> perfStatsSpan(perfStats);

    // Timers
    size_t counterCount = PerfMetrics::SampleCounters<PerfTimerCategory>(perfStatsSpan);

    std::span<PerfStats> sortedCounters = perfStatsSpan.first(counterCount);

    std::ranges::sort(sortedCounters,
        [](const PerfStats& a, const PerfStats& b)
        {
            return a.GetName() < b.GetName();
        });

    for(const auto& counterStat : sortedCounters)
    {
        const std::string text =
            std::format("{}: {:.3f} ms", counterStat.GetName(), counterStat.GetEMA());
        ImGui::TextUnformatted(text.c_str());
    }

    // Other counters
    counterCount = PerfMetrics::SampleCounters<>(perfStatsSpan);

    sortedCounters = perfStatsSpan.first(counterCount);

    std::ranges::sort(sortedCounters,
        [](const PerfStats& a, const PerfStats& b)
        {
            return a.GetName() < b.GetName();
        });

    for(const auto& counterStat : sortedCounters)
    {
        const std::string text =
            std::format("{}: {:.3f}", counterStat.GetName(), counterStat.GetEMA());
        ImGui::TextUnformatted(text.c_str());
    }

    ImGui::End();

    return Result<>::Ok;
}

Result<std::tuple<PropKit, Level, Scene>>
Load(GpuHelper& gpuHelper,
    ThreadPool& threadPool,
    FileFetcher& fileFetcher,
    const std::filesystem::path& path)
{
    PropKitDef propKitDef;
    LevelDef levelDef;
    MLG_CHECK(GltfLoader::Load(path.string(), propKitDef, levelDef),
        "Failed to load glTF file: {}",
        path.string());

    auto propKit =
        PropKit::Create(gpuHelper, threadPool, fileFetcher, path.parent_path(), propKitDef);
    MLG_CHECK(propKit, "Failed to create PropKit for {}", path.string());

    auto level = Level::Create(levelDef, *propKit);
    MLG_CHECK(level, "Failed to create Level for {}", path.string());

    auto scene = Scene::Create(gpuHelper, *level, *propKit);
    MLG_CHECK(scene, "Failed to create Scene for {}", path.string());

    return std::make_tuple(std::move(*propKit), std::move(*level), std::move(*scene));
}

#ifdef _WIN32
constexpr const char* SPONZA_MODEL_PATH = "C:/Users/kbaca/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf";
#else
constexpr const char* SPONZA_MODEL_PATH = "../../assets/main_sponza/NewSponza_Main_glTF_003.gltf";
#endif

Result<>
MainLoop()
{
    auto systemResult = System::Create(APP_NAME);
    MLG_CHECK(systemResult, "Failed to create System");

    System system = std::move(*systemResult);
    GpuHelper& gpuHelper = system.GetGpuHelper();
    ThreadPool& threadPool = system.GetThreadPool();
    FileFetcher& fileFetcher = system.GetFileFetcher();
    Renderer renderer;
    ImGuiRenderer imGuiRenderer;
    WalkMouseNav mouseNav;

    MLG_CHECK(renderer.Startup(gpuHelper, fileFetcher));
    MLG_CHECK(imGuiRenderer.Startup(gpuHelper));

    auto loadResult = Load(gpuHelper, threadPool, fileFetcher, SPONZA_MODEL_PATH);
    MLG_CHECK(loadResult, "Failed to load resources");

    auto&& [propKit, level, scene] = std::move(*loadResult);

    static constexpr float kDefaultCameraHeight = 2.0f;
    static constexpr float kDefaultCameraYaw = 90.0f; // Degrees
    const Radiansf cameraYaw = Radiansf::FromDegrees(kDefaultCameraYaw);

    Dimension2 screenDimensions = gpuHelper.GetScreenDimensions();
    TrTransformf cameraXForm{ .T{0, kDefaultCameraHeight, 0}, .R{UnitQuatf(cameraYaw, Vec3f::YAXIS())} };
    Camera camera((Viewport(screenDimensions)));

    mouseNav.SetTransform(cameraXForm);

    constexpr ActionIdentifier quit("Quit");
    constexpr ActionIdentifier moveForward("MoveForward");
    constexpr ActionIdentifier moveBackward("MoveBackward");
    constexpr ActionIdentifier moveLeft("MoveLeft");
    constexpr ActionIdentifier moveRight("MoveRight");
    constexpr ActionIdentifier moveUpDown("MoveUpDown");
    constexpr ActionIdentifier lookLeftRight("LookLeftRight");
    constexpr ActionIdentifier lookUpDown("LookUpDown");
    constexpr ActionIdentifier captureMouse("CaptureMouse");
    constexpr ActionIdentifier releaseMouse("ReleaseMouse");

    static constexpr float kMouseWheelScale = 20.0f;

    const InputMapping inputMappings[] //
        {
            {
                .Input = KeyPressed(SDL_SCANCODE_ESCAPE),
                .ActionId = quit,
                .Handler =
                    [&](const ActionEvent&)
                {
                    System::PostQuitEvent();
                },
            },
            {
                .Input = KeyDown(SDL_SCANCODE_W),
                .ActionId = moveForward,
                .Handler = [&](const ActionEvent& event)
                { mouseNav.Move(Vec3f(0, 0, event.Value)); },
                .Scale = 1,
            },
            {
                .Input = KeyDown(SDL_SCANCODE_S),
                .ActionId = moveBackward,
                .Handler = [&](const ActionEvent& event)
                { mouseNav.Move(Vec3f(0, 0, event.Value)); },
                .Scale = -1,
            },
            {
                .Input = KeyDown(SDL_SCANCODE_A),
                .ActionId = moveLeft,
                .Handler = [&](const ActionEvent& event)
                { mouseNav.Move(Vec3f(event.Value, 0, 0)); },
                .Scale = -1,
            },
            {
                .Input = KeyDown(SDL_SCANCODE_D),
                .ActionId = moveRight,
                .Handler = [&](const ActionEvent& event)
                { mouseNav.Move(Vec3f(event.Value, 0, 0)); },
                .Scale = 1,
            },
            {
                .Input = MouseMoveX(),
                .ActionId = lookLeftRight,
                .Handler = [&](const ActionEvent& event) { mouseNav.Look(Vec2f(event.Value, 0)); },
                .Scale = WalkMouseNav::kDefualtRotPerDXY * 2 * std::numbers::pi_v<float>,
            },
            {
                .Input = MouseMoveY(),
                .ActionId = lookUpDown,
                .Handler = [&](const ActionEvent& event) { mouseNav.Look(Vec2f(0, event.Value)); },
                .Scale = WalkMouseNav::kDefualtRotPerDXY * 2 * std::numbers::pi_v<float>,
            },
            {
                .Input = MouseWheelY(),
                .ActionId = moveUpDown,
                .Handler = [&](const ActionEvent& event)
                { mouseNav.Move(Vec3f(0, event.Value, 0)); },
                .Scale = kMouseWheelScale,
            },
            {
                .Input = MousePressed(SDL_BUTTON_LEFT),
                .ActionId = captureMouse,
                .Handler =
                    [&](const ActionEvent&)
                {
                    mouseNav.Activate();
                    SDL_SetWindowRelativeMouseMode(gpuHelper.GetWindow(), true);
                },
            },
            {
                .Input = MouseReleased(SDL_BUTTON_LEFT),
                .ActionId = releaseMouse,
                .Handler =
                    [&](const ActionEvent&)
                {
                    mouseNav.Deactivate();
                    SDL_SetWindowRelativeMouseMode(gpuHelper.GetWindow(), false);
                },
            },
        };

    InputMapper inputMapper(inputMappings);

    Timer frameTimer;

    while(!system.ShouldQuit())
    {
        MLG_SCOPED_TIMER("Frame");

        const float elapsedSeconds = frameTimer.GetElapsedSeconds();

        frameTimer.Restart();

        std::string droppedFile;

        auto eventInterceptor = [&](const SDL_Event& sdlEvent)
        {
            ImGui_ImplSDL3_ProcessEvent(&sdlEvent);
            inputMapper.ProcessEvent(sdlEvent);

            switch(sdlEvent.type)
            {
                case SDL_EVENT_DROP_FILE:
                    droppedFile = sdlEvent.drop.data;
                    break;

                default:
                    break;
            }
            return System::EventDisposition::Process;
        };

        system.ProcessEvents(eventInterceptor);

        if(system.IsMinimized())
        {
            std::this_thread::yield();
            continue;
        }

        if(system.ShouldQuit())
        {
            break;
        }

        inputMapper.DispatchEvents();

        if(!droppedFile.empty())
        {
            auto newLoadResult = Load(gpuHelper, threadPool, fileFetcher, SPONZA_MODEL_PATH);
            MLG_CHECK(newLoadResult, "Failed to load resources");

            auto&& [newPropKit, newLevel, newScene] = std::move(*newLoadResult);

            propKit = std::move(newPropKit);
            level = std::move(newLevel);
            scene = std::move(newScene);
        }

        const Dimension2 curScreenDimensions = gpuHelper.GetScreenDimensions();

        if(curScreenDimensions != screenDimensions)
        {
            camera.SetViewport(Viewport(curScreenDimensions));
            screenDimensions = curScreenDimensions;
        }

        mouseNav.Update(elapsedSeconds);
            
        cameraXForm = mouseNav.GetTransform();

        auto target = gpuHelper.GetSwapChainTexture();
        MLG_CHECK(target, "Failed to get swapchain texture");

        MLG_CHECK(renderer.Render(gpuHelper, camera, cameraXForm, scene, propKit));
        MLG_CHECK(renderer.Composite(gpuHelper, *target));

        MLG_CHECK(imGuiRenderer.NewFrame(*target));
        MLG_CHECK(RenderGui());
        MLG_CHECK(imGuiRenderer.Composite(gpuHelper.GetDevice(), *target));

#if !defined(__EMSCRIPTEN__)
        MLG_CHECK(gpuHelper.GetSurface().Present(), "Failed to present backbuffer");
#endif

        gpuHelper.GetInstance().ProcessEvents();
    }

    PerfMetrics::LogCounters();

    return Result<>::Ok;
}
} // namespace

int main(int /*argc*/, char** /*argv*/)
{
    if(!MainLoop())
    {
        return -1;
    }

    return 0;
}