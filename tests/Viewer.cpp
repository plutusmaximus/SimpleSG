#include "Compositor.h"
#include "Renderer.h"
#include "PropKit.h"
#include "ECS.h"
#include "GltfLoader.h"
#include "ImGuiRenderer.h"
#include "MouseNav.h"
#include "PerfMetrics.h"
#include "Projection.h"
#include "Scene.h"
#include "scope_exit.h"
#include "Stopwatch.h"
#include "WebgpuHelper.h"
#include "VecMath.h"

#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <thread>

static constexpr const char* APP_NAME = "Viewer";

static constexpr float PHYSICS_FPS = 100.0f;
static constexpr float RENDER_FPS = 60.0f;

namespace
{

class WorldMatrix : public Mat44f
{
public:
    using Mat44f::Mat44f;

    WorldMatrix& operator=(const Mat44& that)
    {
        this->Mat44f::operator=(that);
        return *this;
    }
};

// Used to tag entities that represent loaded models in the ECS registry.
struct ModelTag{};

}

static Result<>
Startup()
{
    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    MLG_CHECK(WebgpuHelper::Startup(APP_NAME));

    return Result<>::Ok;
}

static void
Shutdown()
{
    WebgpuHelper::Shutdown();
}

static Result<> RenderGui();

static Result<>
LoadPropKit(const std::filesystem::path& path, TextureCache& textureCache, PropKit& outPropKit, Scene& outScene)
{
    PropKitDef propKitDef;
    SceneDef sceneDef;
    MLG_CHECK(GltfLoader::LoadPropKit(path.string(), propKitDef, sceneDef),
        "Failed to load prop kit: {}",
        path.string());

    MLG_CHECK(PropKit::Load(path.parent_path(), textureCache, propKitDef, sceneDef, outPropKit, outScene),
        "Failed to create PropKit for {}",
        path.string());

    return Result<>::Ok;
}

[[maybe_unused]] static constexpr const char* SPONZA_MODEL_PATH = "C:/Users/kbaca/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf";
[[maybe_unused]] static constexpr const char* AVOCADO_MODEL_PATH = "C:/Dev/SimpleSG/assets/glTF-Sample-Assets/Models/Avocado/glTF/Avocado.gltf";
[[maybe_unused]] static constexpr const char* INSTANCE_MODEL_PATH = "C:/Dev/SimpleSG/assets/glTF-Asset-Generator/Output/Positive/Instancing/Instancing_06.gltf";
[[maybe_unused]] static constexpr const char* SPONZA_MODEL_PATH_2 = "C:/Dev/SimpleSG/assets/glTF-Sample-Assets/Models/Sponza/glTF/Sponza.gltf";
[[maybe_unused]] static constexpr const char* JUNGLE_RUINS = "C:/Users/kbaca/Downloads/JungleRuins/GLTF/JungleRuins_Main.gltf";

static Result<>
MainLoop()
{
    bool running = true;
    bool minimized = false;

    const std::filesystem::path path(SPONZA_MODEL_PATH);

    Renderer renderer;
    Compositor compositor;
    ImGuiRenderer imGuiRenderer;
    TextureCache textureCache;
    PropKit propKit;
    Scene scene;
    EcsRegistry registry;
    WalkMouseNav mouseNav;

    MLG_CHECK(renderer.Startup());
    MLG_CHECK(compositor.Startup());
    MLG_CHECK(imGuiRenderer.Startup());
    MLG_CHECK(textureCache.Startup());

    MLG_CHECK(LoadPropKit(path, textureCache, propKit, scene));

    Entity model = registry.CreateEntity(TrsTransformf{}, WorldMatrix{}, ModelTag{});

    Extent screenBounds = WebgpuHelper::GetScreenBounds();

    constexpr Radiansf fov = Radiansf::FromDegrees(45);

    Entity camera = registry.CreateEntity(TrsTransformf{}, WorldMatrix{}, Projection{});
    camera.Get<TrsTransformf>().T = Vec3f{ 0,0,-4 };
    camera.Get<Projection>().SetPerspective(fov, screenBounds, 0.1f, 1000);

    mouseNav.SetTransform(camera.Get<TrsTransformf>());

    uint64_t frameBeginTicks = SDL_GetTicksNS();

    bool mouseCaptured = true;
    SDL_SetWindowRelativeMouseMode(WebgpuHelper::GetWindow(), mouseCaptured);

    while(running)
    {
        PerfMetrics::BeginFrame();

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
                mouseNav.ClearButtons();
                break;

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
                break;

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
                break;

            case SDL_EVENT_DROP_FILE:
                droppedFile = event.drop.data;
                break;

            case SDL_EVENT_DROP_TEXT:
                break;

            case SDL_EVENT_DROP_COMPLETE:
                break;
            }
        }

        if(!droppedFile.empty())
        {
            textureCache.Clear();
            PropKit newPropKit;
            Scene newScene;
            MLG_CHECK(LoadPropKit(droppedFile, textureCache, newPropKit, newScene));
            propKit = std::move(newPropKit);
            scene = std::move(newScene);
        }

        mouseNav.Update(elapsedSeconds);

        screenBounds = WebgpuHelper::GetScreenBounds();
        camera.Get<Projection>().SetBounds(screenBounds);
        camera.Get<TrsTransformf>() = mouseNav.GetTransform();

        // Transform roots
        for(const auto& tuple : registry.GetView<TrsTransformf, WorldMatrix>())
        {
            auto [eid, xform, worldMat] = tuple;
            worldMat = xform.ToMatrix();
        }

        compositor.BeginFrame();
        imGuiRenderer.NewFrame();

        const auto& camWorldMat = camera.Get<WorldMatrix>();
        const auto& projection = camera.Get<Projection>();
        for(const auto& tuple : registry.GetView<WorldMatrix, ModelTag>())
        {
            const auto [eid, worldMat, modelTag] = tuple;

            renderer.Render(camWorldMat, projection, scene, propKit, compositor);
        }

        RenderGui();

        imGuiRenderer.Render(compositor);

        compositor.EndFrame();

#if !defined(__EMSCRIPTEN__)
        MLG_CHECK(WebgpuHelper::GetSurface().Present(), "Failed to present backbuffer");
#endif

        WebgpuHelper::GetInstance().ProcessEvents();

        PerfMetrics::EndFrame();
    }

    MLG_CHECK(textureCache.Shutdown());
    MLG_CHECK(imGuiRenderer.Shutdown());
    MLG_CHECK(compositor.Shutdown());
    MLG_CHECK(renderer.Shutdown());

    PerfMetrics::LogTimers();

    return Result<>::Ok;
}

static Result<> RenderGui()
{
    const char* buildType;
#if defined (NDEBUG)
    buildType = "Release";
#else
    buildType = "Debug";
#endif

    constexpr const char* backend = "Dawn";

    auto title = std::format("Timers: {}/{}", buildType, backend);

    ImGui::SetNextWindowSize(ImVec2(0, 0)); // Auto-fit both width and height
    ImGui::Begin(title.c_str());
    PerfMetrics::TimerStat timers[256];
    unsigned timerCount = PerfMetrics::GetTimers(timers, std::size(timers));
    for(unsigned i = 0; i < timerCount; ++i)
    {
        ImGui::Text("%s: %.3f ms", timers[i].GetName().c_str(), timers[i].GetValue() * 1000.0f);
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

    auto shutdown = scope_exit([]()
    {
        Shutdown();
    });

    if(!MainLoop())
    {
        return -1;
    }

    return 0;
}