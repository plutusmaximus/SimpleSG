#include "Compositor.h"
#include "FileFetcher.h"
#include "GltfLoader.h"
#include "GpuHelper.h"
#include "ImGuiRenderer.h"
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
#include <thread>

namespace
{
constexpr const char* APP_NAME = "Viewer";

Result<>
Startup()
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
Shutdown()
{
    GpuHelper::Shutdown();
    FileFetcher::Shutdown();
    ThreadPool::Shutdown();
}

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

Result<>
MainLoop()
{
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

    bool mouseCaptured = false;

    Timer frameTimer;

    while(running)
    {
        MLG_SCOPED_TIMER("Frame");

        const float elapsedSeconds = frameTimer.GetElapsedSeconds();

        frameTimer.Restart();

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

        std::string droppedFile;

        while(!minimized && running && SDL_PollEvent(&event))
        {
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

            case SDL_EVENT_MOUSE_MOTION:
                if(mouseCaptured)
                {
                    mouseNav.OnMouseMove(Vec2f(event.motion.xrel, event.motion.yrel));
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if(event.button.button == SDL_BUTTON_LEFT)
                {
                    mouseCaptured = true;
                    SDL_SetWindowRelativeMouseMode(GpuHelper::GetWindow(), mouseCaptured);
                }
                break;

            case SDL_EVENT_MOUSE_BUTTON_UP:
                if(event.button.button == SDL_BUTTON_LEFT)
                {
                    mouseCaptured = false;
                    SDL_SetWindowRelativeMouseMode(GpuHelper::GetWindow(), mouseCaptured);
                    mouseNav.ClearButtons();
                }
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                if(mouseCaptured)
                {
                    mouseNav.OnScroll(Vec2f(event.wheel.x, event.wheel.y));
                }
                break;

            case SDL_EVENT_KEY_DOWN:
                if(mouseCaptured)
                {
                    mouseNav.OnKeyDown(event.key.scancode);
                }

                if(SDL_SCANCODE_ESCAPE == event.key.scancode)
                {
                    running = false;
                }
                break;

            case SDL_EVENT_KEY_UP:
                if(mouseCaptured)
                {
                    mouseNav.OnKeyUp(event.key.scancode);
                }
                break;

            case SDL_EVENT_DROP_BEGIN:
            case SDL_EVENT_DROP_TEXT:
            case SDL_EVENT_DROP_COMPLETE:
                break;

            case SDL_EVENT_DROP_FILE:
                droppedFile = event.drop.data;
                break;

            default:
                break;
            }
        }

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
        camera.SetAspectRatio(viewport.GetAspectRatio());
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
    if(!Startup())
    {
        return -1;
    }

    MLG_DEFER
    {
        Shutdown();
    };

    if(!MainLoop())
    {
        return -1;
    }

    return 0;
}