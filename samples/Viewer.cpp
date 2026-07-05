#include "Compositor.h"
#include "FileFetcher.h"
#include "GltfLoader.h"
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
#include "scope_exit.h"
#include "TextureCache.h"
#include "ThreadPool.h"
#include "VecMath.h"

#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_timer.h>
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
Load(const std::filesystem::path& path, TextureCache& textureCache)
{
    PropKitDef propKitDef;
    LevelDef levelDef;
    MLG_CHECK(GltfLoader::Load(path.string(), propKitDef, levelDef),
        "Failed to load glTF file: {}",
        path.string());

    auto propKit = PropKit::Create(path.parent_path(), textureCache, propKitDef);
    MLG_CHECK(propKit, "Failed to create PropKit for {}", path.string());

    auto level = Level::Create(levelDef, *propKit);
    MLG_CHECK(level, "Failed to create Level for {}", path.string());

    auto scene = Scene::Create(*level, *propKit);
    MLG_CHECK(scene, "Failed to create Scene for {}", path.string());

    return std::make_tuple(std::move(*propKit), std::move(*level), std::move(*scene));
}

#ifdef _WIN32
constexpr const char* SPONZA_MODEL_PATH = "C:/Users/kbaca/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf";
#else
constexpr const char* SPONZA_MODEL_PATH = "../../assets/main_sponza/NewSponza_Main_glTF_003.gltf";
#endif

class System
{
public:
    System() = delete;

    static Result<> Startup();
    static void Shutdown();
};

Result<> System::Startup()
{
    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    MLG_CHECK(ThreadPool::Startup());
    MLG_DEFER_AS(failure)
    {
        ThreadPool::Shutdown();
    };

    MLG_CHECK(FileFetcher::Startup());
    MLG_DEFER_AS(fileFetcherShutdown)
    {
        FileFetcher::Shutdown();
    };

    MLG_CHECK(GpuHelper::Startup(APP_NAME));

    failure.release(); // Success, prevent shutdown in defer.
    fileFetcherShutdown.release();

    return Result<>::Ok;
}

void
System::Shutdown()
{
    GpuHelper::Shutdown();
    FileFetcher::Shutdown();
    ThreadPool::Shutdown();
}

Result<>
MainLoop()
{
    MLG_CHECK(System::Startup());

    MLG_DEFER
    {
        System::Shutdown();
    };

    bool running = true;
    bool minimized = false;

    Renderer renderer;
    Compositor compositor;
    ImGuiRenderer imGuiRenderer;
    TextureCache textureCache;
    WalkMouseNav mouseNav;

    MLG_CHECK(renderer.Startup());
    MLG_CHECK(imGuiRenderer.Startup());
    MLG_CHECK(textureCache.Startup());

    auto loadResult = Load(SPONZA_MODEL_PATH, textureCache);
    MLG_CHECK(loadResult, "Failed to load resources");

    auto&& [propKit, level, scene] = std::move(*loadResult);

    Posef cameraXForm{ .T{0, 0, -4} };
    Camera camera((Viewport(GpuHelper::GetScreenBounds())));

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

    InputMapping inputMappings[] //
        {
            {
                .Input = KeyPressed<SDL_SCANCODE_ESCAPE>(),
                .ActionId = quit,
                .Handler =
                    [&](const ActionEvent&)
                {
                    SDL_Event event;

                    event.quit = SDL_QuitEvent //
                        {
                            .type = SDL_EVENT_QUIT,
                            .timestamp = SDL_GetTicksNS(),
                        };

                    SDL_PushEvent(&event);
                },
            },
            {
                .Input = KeyDown<SDL_SCANCODE_W>(),
                .ActionId = moveForward,
                .Handler = [&](const ActionEvent& event)
                { mouseNav.Move(Vec3f(0, 0, event.Value)); },
                .Scale = 1,
            },
            {
                .Input = KeyDown<SDL_SCANCODE_S>(),
                .ActionId = moveBackward,
                .Handler = [&](const ActionEvent& event)
                { mouseNav.Move(Vec3f(0, 0, event.Value)); },
                .Scale = -1,
            },
            {
                .Input = KeyDown<SDL_SCANCODE_A>(),
                .ActionId = moveLeft,
                .Handler = [&](const ActionEvent& event)
                { mouseNav.Move(Vec3f(event.Value, 0, 0)); },
                .Scale = -1,
            },
            {
                .Input = KeyDown<SDL_SCANCODE_D>(),
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
                .Input = MousePressed<SDL_BUTTON_LEFT>(),
                .ActionId = captureMouse,
                .Handler =
                    [&](const ActionEvent&)
                {
                    mouseNav.Activate();
                    SDL_SetWindowRelativeMouseMode(GpuHelper::GetWindow(), true);
                },
            },
            {
                .Input = MouseReleased<SDL_BUTTON_LEFT>(),
                .ActionId = releaseMouse,
                .Handler =
                    [&](const ActionEvent&)
                {
                    mouseNav.Deactivate();
                    SDL_SetWindowRelativeMouseMode(GpuHelper::GetWindow(), false);
                },
            },
        };

    InputMapper inputMapper(std::span(&inputMappings[0], std::size(inputMappings)));

    Timer frameTimer;

    while(running)
    {
        MLG_SCOPED_TIMER("Frame");

        const float elapsedSeconds = frameTimer.GetElapsedSeconds();

        frameTimer.Restart();

        SDL_Event sdlEvent;

        while(minimized && running && SDL_PollEvent(&sdlEvent))
        {
            switch(sdlEvent.type)
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

        std::string droppedFile;

        while(!minimized && running && SDL_PollEvent(&sdlEvent))
        {
            inputMapper.ProcessEvent(sdlEvent);

            switch (sdlEvent.type)
            {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            //case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                {
                    const uint32_t newWidth = static_cast<uint32_t>(sdlEvent.window.data1);
                    const uint32_t newHeight = static_cast<uint32_t>(sdlEvent.window.data2);
                    GpuHelper::Resize(newWidth, newHeight);
                }
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                minimized = true;
                break;

            //case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
            case SDL_EVENT_WINDOW_FOCUS_LOST:
                mouseNav.ClearButtons();
                break;

            case SDL_EVENT_DROP_BEGIN:
            case SDL_EVENT_DROP_TEXT:
            case SDL_EVENT_DROP_COMPLETE:
                break;

            case SDL_EVENT_DROP_FILE:
                droppedFile = sdlEvent.drop.data;
                break;

            default:
                break;
            }
        }

        if(!running)
        {
            continue;
        }

        inputMapper.DispatchEvents();

        if(!droppedFile.empty())
        {
            textureCache.Clear();
            auto newLoadResult = Load(SPONZA_MODEL_PATH, textureCache);
            MLG_CHECK(newLoadResult, "Failed to load resources");

            auto&& [newPropKit, newLevel, newScene] = std::move(*newLoadResult);

            propKit = std::move(newPropKit);
            level = std::move(newLevel);
            scene = std::move(newScene);
        }

        mouseNav.Update(elapsedSeconds);
            
        const Viewport viewport(GpuHelper::GetScreenBounds());
        camera.SetViewport(viewport);
        cameraXForm = mouseNav.GetTransform();

        MLG_CHECK(compositor.BeginFrame());

        MLG_CHECK(renderer.Render(camera, cameraXForm, scene, propKit));
        MLG_CHECK(renderer.Composite(compositor));

        MLG_CHECK(imGuiRenderer.NewFrame());
        MLG_CHECK(RenderGui());
        MLG_CHECK(imGuiRenderer.Composite(compositor));

        MLG_CHECK(compositor.EndFrame());

#if !defined(__EMSCRIPTEN__)
        MLG_CHECK(GpuHelper::GetSurface().Present(), "Failed to present backbuffer");
#endif

        GpuHelper::GetInstance().ProcessEvents();
    }

    MLG_CHECK(textureCache.Shutdown());
    MLG_CHECK(imGuiRenderer.Shutdown());
    MLG_CHECK(renderer.Shutdown());

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