#include "Compositor.h"
#include "GltfLoader.h"
#include "ImGuiRenderer.h"
#include "Level.h"
#include "MouseNav.h"
#include "PerfMetrics.h"
#include "Projection.h"
#include "PropKit.h"
#include "Renderer.h"
#include "Scene.h"
#include "scope_exit.h"
#include "TextureCache.h"
#include "ThreadPool.h"
#include "WebgpuHelper.h"
#include "VecMath.h"

#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <thread>

static constexpr const char* APP_NAME = "Viewer";

static Result<>
Startup()
{
    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    ThreadPool::Startup();
    defer_as(failure)
    {
        ThreadPool::Shutdown();
    };

    MLG_CHECK(WebgpuHelper::Startup(APP_NAME));

    failure.release(); // Success, prevent shutdown in defer.

    return Result<>::Ok;
}

static void
Shutdown()
{
    WebgpuHelper::Shutdown();
    ThreadPool::Shutdown();
}

static Result<> RenderGui();

static Result<>
Load(const std::filesystem::path& path,
    TextureCache& textureCache,
    PropKit& outPropKit,
    Level& outLevel,
    Scene& outScene)
{
    PropKitDef propKitDef;
    LevelDef levelDef;
    MLG_CHECK(GltfLoader::Load(path.string(), propKitDef, levelDef),
        "Failed to load glTF file: {}",
        path.string());

    MLG_CHECK(PropKit::Create(path.parent_path(), textureCache, propKitDef, outPropKit),
        "Failed to create PropKit for {}",
        path.string());

    MLG_CHECK(Level::Create(levelDef, outPropKit, outLevel),
        "Failed to create Level for {}",
        path.string());

    MLG_CHECK(Scene::Create(outLevel, outPropKit, outScene),
        "Failed to create Scene for {}",
        path.string());

    return Result<>::Ok;
}

#ifdef _WIN32
[[maybe_unused]] static constexpr const char* SPONZA_MODEL_PATH = "C:/Users/kbaca/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf";
#else
[[maybe_unused]] static constexpr const char* SPONZA_MODEL_PATH = "../../assets/main_sponza/NewSponza_Main_glTF_003.gltf";
#endif

static Result<>
MainLoop()
{
    bool running = true;
    bool minimized = false;

    Renderer renderer;
    Compositor compositor;
    ImGuiRenderer imGuiRenderer;
    TextureCache textureCache;
    PropKit propKit;
    Level level;
    Scene scene;
    WalkMouseNav mouseNav;

    MLG_CHECK(renderer.Startup());
    MLG_CHECK(imGuiRenderer.Startup());
    MLG_CHECK(textureCache.Startup());

    MLG_CHECK(Load(SPONZA_MODEL_PATH, textureCache, propKit, level, scene));

    TrsTransformf trsCamera{ .T{0, 0, -4} };
    Projection projection;

    mouseNav.SetTransform(trsCamera);

    uint64_t frameBeginTicks = SDL_GetTicksNS();

    bool mouseCaptured = true;
    SDL_SetWindowRelativeMouseMode(WebgpuHelper::GetWindow(), mouseCaptured);

    while(running)
    {
        MLG_SCOPED_TIMER("Frame");

        const uint64_t curTicksNs = SDL_GetTicksNS();
        const uint64_t elapsedTicksNs = curTicksNs - frameBeginTicks;
        const float elapsedSeconds = SDL_NS_TO_SECONDS(static_cast<float>(elapsedTicksNs));
        frameBeginTicks = curTicksNs;

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

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                //app->OnResize(event.window.data1, event.window.data2);
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
            case SDL_EVENT_MOUSE_BUTTON_UP:
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
                else if(SDL_SCANCODE_SPACE == event.key.scancode)
                {
                    mouseCaptured = !mouseCaptured;
                    SDL_SetWindowRelativeMouseMode(WebgpuHelper::GetWindow(), mouseCaptured);
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
            PropKit newPropKit;
            Level newLevel;
            Scene newScene;
            MLG_CHECK(Load(droppedFile, textureCache, newPropKit, newLevel, newScene));
            propKit = std::move(newPropKit);
            level = std::move(newLevel);
            scene = std::move(newScene);
        }

        mouseNav.Update(elapsedSeconds);

        auto screenBounds = WebgpuHelper::GetScreenBounds();
        projection.SetAspectRatio(screenBounds.GetAspectRatio());
        trsCamera = mouseNav.GetTransform();

        compositor.BeginFrame();
        imGuiRenderer.NewFrame();

        renderer.Render(trsCamera, projection, scene, propKit, compositor);

        RenderGui();

        imGuiRenderer.Render(compositor);

        compositor.EndFrame();

#if !defined(__EMSCRIPTEN__)
        MLG_CHECK(WebgpuHelper::GetSurface().Present(), "Failed to present backbuffer");
#endif

        WebgpuHelper::GetInstance().ProcessEvents();
    }

    MLG_CHECK(textureCache.Shutdown());
    MLG_CHECK(imGuiRenderer.Shutdown());
    MLG_CHECK(renderer.Shutdown());

    PerfMetrics::LogTimers();

    return Result<>::Ok;
}

static Result<> RenderGui()
{
#if defined (NDEBUG)
    constexpr const char* buildType = "Release";
#else
    constexpr const char* buildType = "Debug";
#endif

    constexpr const char* backend = "Dawn";

    auto title = std::format("Timers: {}/{}", buildType, backend);

    ImGui::SetNextWindowSize(ImVec2(0, 0)); // Auto-fit both width and height
    ImGui::Begin(title.c_str());

    PerfTimerStats timerStats[256];
    std::span<PerfTimerStats> timerStatsSpan(timerStats);
    const size_t timerCount = PerfMetrics::SampleTimers(timerStatsSpan);
    for(const auto& timerStat : timerStatsSpan.first(timerCount))
    {
        const std::string text =
            std::format("{}: {:.3f} ms", timerStat.GetName(), timerStat.GetEMA() * 1000.0f);
        ImGui::TextUnformatted(text.c_str());
    }

    ImGui::End();

    return Result<>::Ok;
}

int main(int /*argc*/, char** /*argv*/)
{
    if(!Startup())
    {
        return -1;
    }

    defer
    {
        Shutdown();
    };

    if(!MainLoop())
    {
        return -1;
    }

    return 0;
}