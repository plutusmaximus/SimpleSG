#define _CRT_SECURE_NO_WARNINGS
#include <filesystem>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>

#include "AppDriver.h"
#include "Application.h"
#include "Camera.h"
#include "DawnRenderCompositor.h"
#include "DawnRenderer.h"
#include "DawnSceneKit.h"
#include "ECS.h"
#include "EcsChildTransformPool.h"
#include "ImGuiRenderer.h"
#include "Log.h"
#include "MouseNav.h"
#include "PerfMetrics.h"
#include "WebgpuHelper.h"

#include "scope_exit.h"

#include "GltfLoader.h"

namespace
{
static Result<> RenderGui();

class WorldMatrix : public Mat44f
{
public:
    WorldMatrix& operator=(const Mat44& that)
    {
        this->Mat44f::operator=(that);
        return *this;
    }
};

class SponzaApp : public Application
{
public:
    ~SponzaApp() override
    {
    }

    Result<> Initialize() override
    {
        auto cleanup = scope_exit([this]()
        {
            Shutdown();
        });

        MLG_CHECKV(State::None == m_State, "Application already initialized or running");

        m_State = State::Initialized;

        auto rendererResult = DawnRenderer::Create(WebgpuHelper::GetWindow(),
            WebgpuHelper::GetDevice(),
            WebgpuHelper::GetSurface());
        MLG_CHECK(rendererResult);

        auto renderCompositorResult = DawnRenderCompositor::Create();
        MLG_CHECK(renderCompositorResult);

        auto imGuiRendererResult = ImGuiRenderer::Create();
        MLG_CHECK(imGuiRendererResult);

        m_Renderer = *rendererResult;
        m_RenderCompositor = *renderCompositorResult;
        m_ImGuiRenderer = *imGuiRendererResult;

        [[maybe_unused]] constexpr const char* SPONZA_MODEL_PATH = "C:/Users/kbaca/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf";
        [[maybe_unused]] constexpr const char* AVOCADO_MODEL_PATH = "C:/Dev/SimpleSG/assets/glTF-Sample-Assets/Models/Avocado/glTF/Avocado.gltf";
        [[maybe_unused]] constexpr const char* INSTANCE_MODEL_PATH = "C:/Dev/SimpleSG/assets/glTF-Asset-Generator/Output/Positive/Instancing/Instancing_06.gltf";
        [[maybe_unused]] constexpr const char* SPONZA_MODEL_PATH_2 = "C:/Dev/SimpleSG/assets/glTF-Sample-Assets/Models/Sponza/glTF/Sponza.gltf";
        [[maybe_unused]] constexpr const char* JUNGLE_RUINS = "C:/Users/kbaca/Downloads/JungleRuins/GLTF/JungleRuins_Main.gltf";

        std::filesystem::path filePath(SPONZA_MODEL_PATH);

        auto sceneKitData = GltfLoader::LoadSceneKit(filePath.string());
        MLG_CHECK(sceneKitData);

        wgpu::Device wgpuDevice = WebgpuHelper::GetDevice();
        auto dawnSceneKit = DawnSceneKit::Create(wgpuDevice, filePath.parent_path(), *sceneKitData);
        MLG_CHECK(dawnSceneKit);

        SceneKit* sceneKit = *dawnSceneKit;

        m_Model = m_Registry.CreateEntity(TrsTransformf{}, WorldMatrix{}, sceneKit);

        m_ScreenBounds = WebgpuHelper::GetScreenBounds();

        constexpr Radiansf fov = Radiansf::FromDegrees(45);

        m_Camera = m_Registry.CreateEntity(TrsTransformf{}, WorldMatrix{}, Camera{});
        m_Camera.Get<TrsTransformf>().T = Vec3f{ 0,0,-4 };
        m_Camera.Get<Camera>().SetPerspective(fov, m_ScreenBounds, 0.1f, 1000);

        m_WalkMouseNav.SetTransform(m_Camera.Get<TrsTransformf>());

        m_State = State::Running;

        cleanup.release();

        return Result<>::Ok;
    }

    void Shutdown() override
    {
        if(State::Shutdown == m_State)
        {
            return;
        }
        m_State = State::Shutdown;

        m_Registry.Clear();

        ImGuiRenderer::Destroy(m_ImGuiRenderer);
        DawnRenderCompositor::Destroy(m_RenderCompositor);
        DawnRenderer::Destroy(m_Renderer);

        m_Renderer = nullptr;
        m_RenderCompositor = nullptr;
        m_ImGuiRenderer = nullptr;
    }

    void Update(const float deltaSeconds) override
    {
        if(!MLG_VERIFY(State::Running == m_State, "Application is not running"))
        {
            return;
        }

        static PerfTimer frameTimer("Frame");
        auto scopedFrameTimer = frameTimer.StartScoped();

        m_ScreenBounds = WebgpuHelper::GetScreenBounds();

        m_Camera.Get<Camera>().SetBounds(m_ScreenBounds);

        m_MouseNav->Update(deltaSeconds);

        m_Camera.Get<TrsTransformf>() = m_MouseNav->GetTransform();

        // Transform roots
        for(const auto& tuple : m_Registry.GetView<TrsTransformf, WorldMatrix>())
        {
            auto [eid, xform, worldMat] = tuple;
            worldMat = xform.ToMatrix();
        }

        m_RenderCompositor->BeginFrame();

        m_ImGuiRenderer->NewFrame();

        // Transform to camera space and render
        auto camWorldMat = m_Camera.Get<WorldMatrix>();
        auto camera = m_Camera.Get<Camera>();
        for(const auto& tuple : m_Registry.GetView<WorldMatrix, SceneKit*>())
        {
            const auto [eid, worldMat, sceneKit] = tuple;

            m_Renderer->Render(camWorldMat, camera.GetProjection(), *sceneKit, m_RenderCompositor);
        }

        RenderGui();

        m_ImGuiRenderer->Render(m_RenderCompositor);

        m_RenderCompositor->EndFrame();
    }

    bool IsRunning() const override
    {
        return State::Running == m_State;
    }

    void OnMouseDown(const Point& mouseLoc, const int mouseButton) override
    {
        m_MouseNav->OnMouseDown(mouseLoc, m_ScreenBounds, mouseButton);
    }

    void OnMouseUp(const int mouseButton) override
    {
        m_MouseNav->OnMouseUp(mouseButton);
    }

    void OnKeyDown(const int keyCode) override
    {
        m_MouseNav->OnKeyDown(keyCode);
        if(SDL_SCANCODE_ESCAPE == keyCode)
        {
            m_State = State::ShutdownRequested;
        }
    }

    void OnKeyUp(const int keyCode) override
    {
        m_MouseNav->OnKeyUp(keyCode);
    }

    void OnScroll(const Vec2f& scroll) override
    {
        m_MouseNav->OnScroll(scroll);
    }

    void OnMouseMove(const Vec2f& mouseDelta) override
    {
        m_MouseNav->OnMouseMove(mouseDelta);
    }

    void OnFocusGained() override
    {
        m_MouseNav->ClearButtons();
    }

    void OnFocusLost() override
    {
        m_MouseNav->ClearButtons();
    }

private:

        enum class State
        {
            None,
            Initialized,
            Running,
            ShutdownRequested,
            Shutdown
        };

        State m_State = State::None;

        DawnRenderCompositor* m_RenderCompositor = nullptr;
        DawnRenderer* m_Renderer = nullptr;
        ImGuiRenderer* m_ImGuiRenderer = nullptr;
        EcsRegistry m_Registry;
        WalkMouseNav m_WalkMouseNav{ TrsTransformf{}, 0.0001f, 5.0f };
        MouseNav* const m_MouseNav = &m_WalkMouseNav;
        Entity m_Camera;
        Entity m_Model;
        Extent m_ScreenBounds{0,0};
};

class SponzaAppLifecycle : public AppLifecycle
{
public:
    Application* Create() override
    {
        return new SponzaApp();
    }

    void Destroy(Application* app) override
    {
        delete app;
    }

    std::string_view GetName() const override
    {
        return "Sponza";
    }
};

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
}

int main(int, char* /*argv[]*/)
{
    SponzaAppLifecycle appLifecycle;
    AppDriver driver(&appLifecycle);

    auto initResult = driver.Init();
    if(!initResult)
    {
        return -1;
    }

    driver.SetMouseCapture(true);

    auto runResult = driver.Run();

    PerfMetrics::LogTimers();

    if(!runResult)
    {
        return -1;
    }

    return 0;
}