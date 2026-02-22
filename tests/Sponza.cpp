#define _CRT_SECURE_NO_WARNINGS
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL.h>

#include "AppDriver.h"
#include "Application.h"
#include "Camera.h"
#include "ECS.h"
#include "EcsChildTransformPool.h"
#include "GpuDevice.h"
#include "Logging.h"
#include "MouseNav.h"
#include "PerfMetrics.h"
#include "ResourceCache.h"

#include "scope_exit.h"

static Result<void> RenderGui();

static Result<GpuPipeline*> CreatePipeline(ResourceCache* cache);

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

    Result<void> Initialize(AppContext* context) override
    {
        auto cleanup = scope_exit([this]()
        {
            Shutdown();
        });

        if(!everify(State::None == m_State, "Application already initialized or running"))
        {
            return Error("Application already initialized or running");
        }

        m_State = State::Initialized;

        m_GpuDevice = context->GpuDevice;
        m_ResourceCache = context->ResourceCache;

        auto pipelineResult = CreatePipeline(m_ResourceCache);
        expect(pipelineResult, pipelineResult.error());

        auto rendererResult = m_GpuDevice->CreateRenderer(pipelineResult.value());
        expect(rendererResult, rendererResult.error());

        m_Renderer = *rendererResult;

        [[maybe_unused]] constexpr const char* SPONZA_MODEL_PATH = "C:/Users/kbaca/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf";
        [[maybe_unused]] constexpr const char* AVOCADO_MODEL_PATH = "C:/Dev/SimpleSG/assets/glTF-Sample-Assets/Models/Avocado/glTF/Avocado.gltf";
        [[maybe_unused]] constexpr const char* INSTANCE_MODEL_PATH = "C:/Dev/SimpleSG/assets/glTF-Asset-Generator/Output/Positive/Instancing/Instancing_06.gltf";
        [[maybe_unused]] constexpr const char* SPONZA_MODEL_PATH_2 = "C:/Dev/SimpleSG/assets/glTF-Sample-Assets/Models/Sponza/glTF/Sponza.gltf";
        [[maybe_unused]] constexpr const char* JUNGLE_RUINS = "C:/Users/kbaca/Downloads/JungleRuins/GLTF/JungleRuins_Main.gltf";

        const CacheKey cacheKey("Sponza");
        auto statusResult = m_ResourceCache->LoadModelFromFileAsync(cacheKey, SPONZA_MODEL_PATH);
        expect(statusResult, statusResult.error());

        while(statusResult.value().IsPending())
        {
            m_ResourceCache->ProcessPendingOperations();
        }

        auto modelResult = m_ResourceCache->GetModel(cacheKey);
        expect(modelResult, modelResult.error());

        auto model = *modelResult;

        m_EidModel = m_Registry.Create();
        m_Registry.Add(m_EidModel, TrsTransformf{}, WorldMatrix{}, model);

        m_EidCamera = m_Registry.Create();

        m_ScreenBounds = m_GpuDevice->GetScreenBounds();

        constexpr Radiansf fov = Radiansf::FromDegrees(45);

        m_Registry.Add(m_EidCamera, TrsTransformf{}, WorldMatrix{}, Camera{});
        m_Registry.Get<TrsTransformf>(m_EidCamera).T = Vec3f{ 0,0,-4 };
        m_Registry.Get<Camera>(m_EidCamera).SetPerspective(fov, m_ScreenBounds, 0.1f, 1000);
        m_WalkMouseNav.SetTransform(m_Registry.Get<TrsTransformf>(m_EidCamera));

        m_State = State::Running;

        cleanup.release();

        return Result<void>::Success;
    }

    void Shutdown() override
    {
        if(State::Shutdown == m_State)
        {
            return;
        }
        m_State = State::Shutdown;

        m_Registry.Clear();

        if(m_Renderer)
        {
            m_GpuDevice->DestroyRenderer(m_Renderer);
        }
        m_GpuDevice = nullptr;
        m_Renderer = nullptr;
        m_ResourceCache = nullptr;
    }

    void Update(const float deltaSeconds) override
    {
        if(!everify(State::Running == m_State, "Application is not running"))
        {
            return;
        }

        m_ScreenBounds = m_GpuDevice->GetScreenBounds();

        m_Registry.Get<Camera>(m_EidCamera).SetBounds(m_ScreenBounds);

        m_MouseNav->Update(deltaSeconds);

        m_Registry.Get<TrsTransformf>(m_EidCamera) = m_MouseNav->GetTransform();

        // Transform roots
        for(const auto& tuple : m_Registry.GetView<TrsTransformf, WorldMatrix>())
        {
            auto [eid, xform, worldMat] = tuple;
            worldMat = xform.ToMatrix();
        }

        // Transform parent/child relationships
        for(const auto& tuple : m_Registry.GetView<ChildTransform, WorldMatrix>())
        {
            auto [eid, xform, worldMat] = tuple;

            const EntityId parentId = xform.ParentId;
            if(!parentId.IsValid())
            {
                worldMat = xform.LocalTransform.ToMatrix();
            }
            else
            {
                const auto parentWorldMat = m_Registry.Get<WorldMatrix>(parentId);
                worldMat = parentWorldMat * xform.LocalTransform.ToMatrix();
            }
        }

        m_Renderer->NewFrame();

        // Transform to camera space and render
        auto cameraTuple = m_Registry.Get<WorldMatrix, Camera>(m_EidCamera);
        for(const auto& tuple : m_Registry.GetView<WorldMatrix, ModelResource>())
        {
            const auto [eid, worldMat, model] = tuple;
            m_Renderer->AddModel(worldMat, model.Get());
        }

        RenderGui();

        const auto [camWorldMat, camera] = cameraTuple;
        auto renderResult = m_Renderer->Render(camWorldMat, camera.GetProjection());
        if (!renderResult)
        {
            logError(renderResult.error().GetMessage());
        }
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

        GpuDevice* m_GpuDevice = nullptr;
        ResourceCache* m_ResourceCache = nullptr;
        Renderer* m_Renderer = nullptr;
        EcsRegistry m_Registry;
        WalkMouseNav m_WalkMouseNav{ TrsTransformf{}, 0.0001f, 5.0f };
        MouseNav* const m_MouseNav = &m_WalkMouseNav;
        EntityId m_EidCamera;
        EntityId m_EidModel;
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

int main(int, char* /*argv[]*/)
{
    SponzaAppLifecycle appLifecycle;
    AppDriver driver(&appLifecycle);

    auto initResult = driver.Init();
    if(!initResult)
    {
        logError(initResult.error().GetMessage());
        return -1;
    }

    driver.SetMouseCapture(true);

    auto runResult = driver.Run();

    PerfMetrics::LogTimers();

    if(!runResult)
    {
        logError(runResult.error().GetMessage());
        return -1;
    }

    return 0;
}

static Result<GpuPipeline*> CreatePipeline(ResourceCache* cache)
{
    const PipelineSpec pipelineSpec//
    {
        .PipelineType = GpuPipelineType::Opaque,
        .VertexShader{"shaders/Debug/VertexShader.vs", 3},
    #if DAWN_GPU
        .FragmentShader{"shaders/Debug/FragmentShader.fs"},
    #else
        .FragmentShader{"shaders/Debug/FragmentShader.ps"},
    #endif
    };

    const CacheKey pipelineCacheKey("MainPipeline");
    auto asyncResult = cache->CreatePipelineAsync(pipelineCacheKey, pipelineSpec);
    expect(asyncResult, asyncResult.error());

    while(asyncResult->IsPending())
    {
        cache->ProcessPendingOperations();
    }

    return cache->GetPipeline(pipelineCacheKey);
}

static Result<void> RenderGui()
{
    ImGui::Begin("Timers");
    PerfMetrics::TimerStat timers[256];
    unsigned timerCount = PerfMetrics::GetTimers(timers, std::size(timers));
    for(unsigned i = 0; i < timerCount; ++i)
    {
        ImGui::Text("%s: %.3f ms", timers[i].GetName().c_str(), timers[i].GetValue() * 1000.0f);
    }
    ImGui::End();

    return Result<void>::Success;
}