#include "Camera.h"
#include "GltfLoader.h"
#include "GpuColorPass.h"
#include "GpuHelper.h"
#include "ImGuiRenderer.h"
#include "InputMapper.h"
#include "Level.h"
#include "MouseNav.h"
#include "PerfMetrics.h"
#include "PropKit.h"
#include "Renderer.h"
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

Result<std::tuple<PropKit, Level, Scene>>
LoadLevel(GpuHelper& gpuHelper,
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

    auto scene = Scene::Create(gpuHelper, *level);
    MLG_CHECK(scene, "Failed to create Scene for {}", path.string());

    return std::make_tuple(std::move(*propKit), std::move(*level), std::move(*scene));
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
    auto task = System::Create(kAppName);
    MLG_CHECK(task, "Failed to create System");

    while(!task->IsComplete())
    {
        MLG_CHECK(task->Update());
    }

    MLG_CHECK(task->Succeeded(), "System creation failed");
    auto systemResult = task->Get();
    MLG_CHECK(systemResult, "Failed to get System instance");

    System& system = *systemResult;
    GpuHelper& gpuHelper = system.GetGpuHelper();
    ThreadPool& threadPool = system.GetThreadPool();
    FileFetcher& fileFetcher = system.GetFileFetcher();
    Renderer& renderer = system.GetRenderer();
    const ImGuiRenderer& imGuiRenderer = system.GetImGuiRenderer();

    WalkMouseNav mouseNav;

    auto loadResult = LoadLevel(gpuHelper, threadPool, fileFetcher, SPONZA_MODEL_PATH);
    MLG_CHECK(loadResult, "Failed to load resources");

    auto&& [propKit, level, scene] = std::move(*loadResult);

    static constexpr float kDefaultCameraHeight = 2.0f;
    static constexpr float kDefaultCameraYaw = 90.0f; // Degrees

    const Radiansf cameraYaw = Radiansf::FromDegrees(kDefaultCameraYaw);

    Dimension2 screenDimensions = gpuHelper.GetScreenDimensions();
    TrTransformf cameraXForm{ .T{ 0, kDefaultCameraHeight, 0 }, .R{ cameraYaw, Vec3f::YAXIS() } };
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

    const ActionMapping actionMappings[] //
        {
            {
                .ActionId = quit,
                .Input = InputButton::KeyPressed(SDL_SCANCODE_ESCAPE),
            },
            {
                .ActionId = moveForward,
                .Input = InputButton::KeyDown(SDL_SCANCODE_W),
                .Scale = 1,
            },
            {
                .ActionId = moveBackward,
                .Input = InputButton::KeyDown(SDL_SCANCODE_S),
                .Scale = -1,
            },
            {
                .ActionId = moveLeft,
                .Input = InputButton::KeyDown(SDL_SCANCODE_A),
                .Scale = -1,
            },
            {
                .ActionId = moveRight,
                .Input = InputButton::KeyDown(SDL_SCANCODE_D),
                .Scale = 1,
            },
            {
                .ActionId = lookLeftRight,
                .Input = InputAxis::MouseMoveX,
                .Scale = WalkMouseNav::kDefualtRotPerDXY * 2 * std::numbers::pi_v<float>,
            },
            {
                .ActionId = lookUpDown,
                .Input = InputAxis::MouseMoveY,
                .Scale = WalkMouseNav::kDefualtRotPerDXY * 2 * std::numbers::pi_v<float>,
            },
            {
                .ActionId = moveUpDown,
                .Input = InputAxis::MouseWheelY,
                .Scale = kMouseWheelScale,
            },
            {
                .ActionId = captureMouse,
                .Input = InputButton::MousePressed(SDL_BUTTON_LEFT),
            },
            {
                .ActionId = releaseMouse,
                .Input = InputButton::MouseReleased(SDL_BUTTON_LEFT),
            },
        };

    InputMapper inputMapper(actionMappings);

    Timer frameTimer;

    while(!system.ShouldQuit())
    {
        MLG_SCOPED_TIMER("Frame");

        const float elapsedSeconds = frameTimer.GetElapsedSeconds();

        frameTimer.Restart();

        inputMapper.BeginFrame();

        struct EventHandlerData
        {
            std::string droppedFile;
            InputMapper* inputMapper;
        };

        EventHandlerData eventHandlerData{ .droppedFile = "", .inputMapper = &inputMapper };

        auto eventHandlerFunc = [](const SDL_Event& sdlEvent, EventHandlerData* data)
        {
            data->inputMapper->ProcessEvent(sdlEvent);

            switch(sdlEvent.type)
            {
                case SDL_EVENT_DROP_FILE:
                    data->droppedFile = sdlEvent.drop.data;
                    break;

                default:
                    break;
            }
            return EventDisposition::Process;
        };

        const EventHandler eventHandler(+eventHandlerFunc, &eventHandlerData);

        system.ProcessEvents(eventHandler);

        inputMapper.EndFrame();

        if(system.IsMinimized())
        {
            std::this_thread::yield();
            continue;
        }

        if(system.ShouldQuit())
        {
            break;
        }

        if(system.WasMinimized()
            || system.WasRestored()
            || system.WasFocusGained()
            || system.WasFocusLost())
        {
            inputMapper.Clear();
        }

        float actionValue = 0;

        if(inputMapper.Action(quit))
        {
            System::PostQuitEvent();
        }
        if(inputMapper.Action(moveForward, actionValue))
        {
            mouseNav.Move(Vec3f(0, 0, actionValue));
        }
        // Removed redundant block
        if(inputMapper.Action(moveBackward, actionValue))
        {
            mouseNav.Move(Vec3f(0, 0, actionValue));
        }
        if(inputMapper.Action(moveLeft, actionValue))
        {
            mouseNav.Move(Vec3f(actionValue, 0, 0));
        }
        if(inputMapper.Action(moveRight, actionValue))
        {
            mouseNav.Move(Vec3f(actionValue, 0, 0));
        }
        if(inputMapper.Action(moveUpDown, actionValue))
        {
            mouseNav.Move(Vec3f(0, actionValue, 0));
        }
        if(inputMapper.Action(lookLeftRight, actionValue))
        {
            mouseNav.Look(Vec2f(actionValue, 0));
        }
        if(inputMapper.Action(lookUpDown, actionValue))
        {
            mouseNav.Look(Vec2f(0, actionValue));
        }
        if(inputMapper.Action(captureMouse))
        {
            mouseNav.Activate();
            system.SetMouseCaptured(true);
        }
        if(inputMapper.Action(releaseMouse))
        {
            mouseNav.Deactivate();
            system.SetMouseCaptured(false);
        }

        if(!eventHandlerData.droppedFile.empty())
        {
            auto newLoadResult = LoadLevel(gpuHelper, threadPool, fileFetcher, SPONZA_MODEL_PATH);
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
        MLG_CHECKV(target, "Failed to get swap chain texture");

        MLG_CHECK(renderer.Render(camera, cameraXForm, scene, propKit));
        MLG_CHECK(renderer.Composite(*target));

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