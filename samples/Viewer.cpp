#include "CameraActor.h"
#include "GltfLoader.h"
#include "GpuColorPass.h"
#include "GpuHelper.h"
#include "ImGuiRenderer.h"
#include "InputMapper.h"
#include "Level.h"
#include "LevelDefs.h"
#include "PerfMetrics.h"
#include "ResourceBundle.h"
#include "Scene.h"
#include "System.h"
#include "VecMath.h"

#include <filesystem>
#include <imgui.h>
#include <SDL3/SDL_events.h>
#include <thread>

namespace
{
constexpr const char* kAppName = "Viewer";

Result<>
RenderGui()
{
#if defined(NDEBUG)
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
        [](const PerfStats& a, const PerfStats& b) { return a.GetName() < b.GetName(); });

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
        [](const PerfStats& a, const PerfStats& b) { return a.GetName() < b.GetName(); });

    for(const auto& counterStat : sortedCounters)
    {
        const std::string text =
            std::format("{}: {:.3f}", counterStat.GetName(), counterStat.GetEMA());
        ImGui::TextUnformatted(text.c_str());
    }

    ImGui::End();

    return Result<>::Ok;
}

Result<std::tuple<std::unique_ptr<Level>, std::unique_ptr<Scene>>>
LoadLevel(System& system, const std::filesystem::path& path)
{
    PropKitDef propKitDef;
    LevelDef levelDef;
    MLG_CHECK(GltfLoader::Load(path.string(), propKitDef, levelDef),
        "Failed to load glTF file: {}",
        path.string());

    ResourceBundleBuilder builder;
    auto rsrcBundle = builder.Build(levelDef, propKitDef);
    MLG_CHECK(rsrcBundle, "Failed to build ResourceBundle");

    auto levelResult = Level::Create(*rsrcBundle);
    MLG_CHECK(levelResult, "Failed to create Level for {}", path.string());

    std::unique_ptr<Level> level = std::move(*levelResult);

    const std::filesystem::path rootPath = path.parent_path();

    Scene::CreateTask createTask(system, rootPath, *rsrcBundle, *level);

    MLG_CHECK(createTask.Begin(), "Failed to begin create task");

    while(createTask.IsPending())
    {
        createTask.Update();
    }

    auto sceneResult = createTask.Take();
    MLG_CHECK(sceneResult, "Failed to create Scene");

    std::unique_ptr<Scene> scene = std::move(*sceneResult);

    return std::make_tuple(std::move(level), std::move(scene));
}

#ifdef _WIN32
constexpr const char* SPONZA_MODEL_PATH =
    "C:/Users/kbaca/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf";
#else
constexpr const char* SPONZA_MODEL_PATH =
    "../../../assets/main_sponza/NewSponza_Main_glTF_003.gltf";
#endif

Result<>
MainLoop()
{
    Log::SetLevel(Log::Level::Trace);

    System::CreateTask sysCreateTask(kAppName);

    MLG_CHECK(sysCreateTask.Begin());

    while(sysCreateTask.IsPending())
    {
        sysCreateTask.Update();
    }

    auto systemResult = sysCreateTask.Take();
    MLG_CHECK(systemResult, "Failed to get create System");

    System& system = *systemResult;

    CameraActor cameraActor;

    auto loadResult = LoadLevel(system, SPONZA_MODEL_PATH);
    MLG_CHECK(loadResult, "Failed to load resources");

    auto&& [level, scene] = std::move(*loadResult);

    static constexpr float kDefaultCameraHeight = 2.0f;
    static constexpr float kDefaultCameraYaw = 90.0f; // Degrees

    const Radiansf cameraYaw = Radiansf::FromDegrees(kDefaultCameraYaw);

    const GpuHelper& gpuHelper = system.GetGpuHelper();

    Dimension2 screenDimensions = gpuHelper.GetScreenDimensions();
    TrTransformf cameraXForm{ .T{ 0, kDefaultCameraHeight, 0 }, .R{ cameraYaw, Vec3f::YAXIS() } };
    Camera camera((Viewport(screenDimensions)));

    cameraActor.SetTransform(cameraXForm);

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

    const ActionMapping actionMappings[] //
        {
            {
                .ActionId = quit,
                .Trigger = InputButton::KeyPressed(SDL_SCANCODE_ESCAPE),
            },
            {
                .ActionId = moveForward,
                .Trigger = InputButton::KeyHeld(SDL_SCANCODE_W),
                .Scale = 1,
            },
            {
                .ActionId = moveBackward,
                .Trigger = InputButton::KeyHeld(SDL_SCANCODE_S),
                .Scale = -1,
            },
            {
                .ActionId = moveLeft,
                .Trigger = InputButton::KeyHeld(SDL_SCANCODE_A),
                .Scale = -1,
            },
            {
                .ActionId = moveRight,
                .Trigger = InputButton::KeyHeld(SDL_SCANCODE_D),
                .Scale = 1,
            },
            {
                .ActionId = lookLeftRight,
                .Trigger = InputAxis::MouseMoveX(),
                .Scale = CameraActor::kDefaultRotPerMouseMove * 2 * std::numbers::pi_v<float>,
            },
            {
                .ActionId = lookUpDown,
                .Trigger = InputAxis::MouseMoveY(),
                .Scale = CameraActor::kDefaultRotPerMouseMove * 2 * std::numbers::pi_v<float>,
            },
            {
                .ActionId = moveUpDown,
                .Trigger = InputAxis::MouseWheelY(),
                .Scale = kMouseWheelScale,
            },
            {
                .ActionId = captureMouse,
                .Trigger = InputButton::MousePressed(SDL_BUTTON_LEFT),
            },
            {
                .ActionId = releaseMouse,
                .Trigger = InputButton::MouseReleased(SDL_BUTTON_LEFT),
            },
        };

    system.SetActionMapping(actionMappings);

    Timer frameTimer;

    bool isCameraActorActive = false;

    while(!system.ShouldQuit())
    {
        MLG_SCOPED_TIMER("Frame");

        const float elapsedSeconds = frameTimer.GetElapsedSeconds();

        frameTimer.Restart();

        system.ProcessEvents();

        if(system.IsMinimized())
        {
            std::this_thread::yield();
            continue;
        }

        if(system.ShouldQuit())
        {
            break;
        }

        const InputMapper& inputMapper = system.GetInputMapper();

        if(inputMapper.IsActionTriggered(quit))
        {
            System::PostQuitEvent();
        }
        if(inputMapper.IsActionTriggered(captureMouse))
        {
            isCameraActorActive = true;
            system.SetMouseCaptured(true);
        }
        if(inputMapper.IsActionTriggered(releaseMouse))
        {
            isCameraActorActive = false;
            system.SetMouseCaptured(false);
        }

        const Dimension2 curScreenDimensions = gpuHelper.GetScreenDimensions();

        if(curScreenDimensions != screenDimensions)
        {
            camera.SetViewport(Viewport(curScreenDimensions));
            screenDimensions = curScreenDimensions;
        }

        if(isCameraActorActive)
        {
            cameraActor.Update(inputMapper, elapsedSeconds);
        }
        cameraXForm = cameraActor.GetTransform();

        auto target = gpuHelper.GetSwapChainTexture();
        MLG_CHECKV(target, "Failed to get swap chain texture");

        MLG_CHECK(scene->Render(camera, cameraXForm));
        MLG_CHECK(scene->Composite(*target));

        const ImGuiRenderer& imGuiRenderer = system.GetImGuiRenderer();
        MLG_CHECK(imGuiRenderer.Render(gpuHelper.GetDevice(), *target, RenderGui));

#if !defined(__EMSCRIPTEN__)
        MLG_CHECK(gpuHelper.GetSurface().Present(), "Failed to present backbuffer");
#endif
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