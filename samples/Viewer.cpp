#include "CameraActor.h"
#include "CommonActionIds.h"
#include "CoopTask.h"
#include "FilePathHelper.h"
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

#include <imgui.h>
#include <SDL3/SDL_events.h>
#include <string>
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

    const PerfStats* perfStats[kMaxPerfStats];
    std::span<const PerfStats*> perfStatsSpan(perfStats);

    // Timers
    size_t counterCount = PerfMetrics::SampleCounters<PerfTimerCategory>(perfStatsSpan);

    std::span<const PerfStats*> sortedCounters = perfStatsSpan.first(counterCount);

    std::ranges::sort(sortedCounters, {}, &PerfStats::GetName);

    for(const auto* counterStat : sortedCounters)
    {
        const auto text = FixedString<256>::Format("{}: {:.3f} ms",
            counterStat->GetName(),
            counterStat->GetEMA());
        ImGui::TextUnformatted(text.c_str());
    }

    // Other counters
    counterCount = PerfMetrics::SampleCounters<>(perfStatsSpan);

    sortedCounters = perfStatsSpan.first(counterCount);

    std::ranges::sort(sortedCounters, {}, &PerfStats::GetName);

    for(const auto* counterStat : sortedCounters)
    {
        const auto text =
            FixedString<256>::Format("{}: {:.3f}", counterStat->GetName(), counterStat->GetEMA());
        ImGui::TextUnformatted(text.c_str());
    }

    ImGui::End();

    return Result<>::Ok;
}

Result<std::tuple<std::unique_ptr<Level>, std::unique_ptr<Scene>>>
LoadLevel(System& system, const std::string_view path)
{
    auto loadResult = GltfLoader::Load(path);
    MLG_CHECK(loadResult, "Failed to load glTF file: {}", path);
    const LevelDef levelDef = std::move(*loadResult);

    ResourceBundleBuilder builder;
    auto rsrcBundle = builder.Build(levelDef);
    MLG_CHECK(rsrcBundle, "Failed to build ResourceBundle");

    auto levelResult = Level::Create(*rsrcBundle);
    MLG_CHECK(levelResult, "Failed to create Level for {}", path);

    std::unique_ptr<Level> level = std::move(*levelResult);

    const std::string_view parentDir = FilePathHelper::GetParent(path);
    auto parentPath = DirectoryPath::Create(parentDir);
    MLG_CHECK(parentPath, "Failed to create parent path");

    Scene::CreateTask createTask(system, *parentPath, *rsrcBundle, *level);

    MLG_CHECK(createTask.Start(), "Failed to begin create task");

    while(createTask.IsRunning())
    {
        createTask.Update();
    }

    auto sceneResult = createTask.Take();
    MLG_CHECK(sceneResult, "Failed to create Scene");

    std::unique_ptr<Scene> scene = std::move(*sceneResult);

    return std::make_tuple(std::move(level), std::move(scene));
}

constexpr const char* SPONZA_MODEL_PATH = "main_sponza/NewSponza_Main_glTF_003.gltf";

Result<>
MainLoop()
{
    System::CreateTask sysCreateTask(kAppName);

    MLG_CHECK(sysCreateTask.Start());

    while(sysCreateTask.IsRunning())
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

    static constexpr float kMouseWheelScale = 20.0f;

    constexpr ActionMapping actionMappings[] //
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

        MLG_CHECK(gpuHelper.Present(), "Failed to present backbuffer");
    }

    PerfMetrics::LogCounters();

    return Result<>::Ok;
}

class Viewer : public ICoopTask<>
{
public:
    Viewer() = default;

    Result<> OnStart() override;

    void OnUpdate() override;
};

Result<>
Viewer::OnStart()
{
    // Implement the start logic for the viewer here.
    return Result<>::Ok;
}

void
Viewer::OnUpdate()
{
    // Implement the update logic for the viewer here.
}

} // namespace

int
main(int /*argc*/, char** /*argv*/)
{
    Log::SetLevel(Log::Level::Trace);

    if(!MainLoop())
    {
        return -1;
    }

    return 0;
}