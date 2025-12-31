#include <SDL3/SDL.h>

#include "AppDriver.h"
#include "Application.h"
#include "Camera.h"
#include "ECS.h"
#include "EcsChildTransformPool.h"
#include "ModelCatalog.h"
#include "MouseNav.h"
#include "SDLGPUDevice.h"
#include "SDLRenderGraph.h"

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
    ~SponzaApp() override = default;

    std::string_view GetName() const override
    {
        return "Sponza";
    }

    Result<void> Initialize(RefPtr<SDLGPUDevice> gpuDevice) override
    {
        if(!everify(State::None == m_State, "Application already initialized or running"))
        {
            return std::unexpected(Error("Application already initialized or running"));
        }

        m_State = State::Initialized;

        m_RenderGraph = new SDLRenderGraph(gpuDevice.Get());

        if(!m_RenderGraph)
        {
            Shutdown();
            return std::unexpected(Error("Failed to create SDLRenderGraph"));
        }

        m_GPUDevice = gpuDevice;
        auto modelSpec= m_ModelCatalog.LoadFromFile("Sponza", "C:/Users/kbaca/Downloads/main_sponza/NewSponza_Main_glTF_003.gltf");
        expect(modelSpec, modelSpec.error());

        auto model = m_GPUDevice->CreateModel(*modelSpec);
        expect(model, model.error());

        m_EidModel = m_Registry.Create();
        m_Registry.Add(m_EidModel, TrsTransformf{}, WorldMatrix{}, *model);

        m_EidCamera = m_Registry.Create();

        m_ScreenBounds = m_GPUDevice->GetExtent();
        
        m_Registry.Add(m_EidCamera, TrsTransformf{}, WorldMatrix{}, Camera{});
        m_Registry.Get<TrsTransformf>(m_EidCamera).T = Vec3f{ 0,0,-4 };
        m_Registry.Get<Camera>(m_EidCamera).SetPerspective(Degreesf(45), m_ScreenBounds, 0.1f, 1000);
        m_WalkMouseNav.SetTransform(m_Registry.Get<TrsTransformf>(m_EidCamera));

        m_State = State::Running;

        return {};
    }

    void Shutdown() override
    {
        if(State::Shutdown == m_State)
        {
            return;
        }
        m_State = State::Shutdown;

        delete m_RenderGraph;
        m_RenderGraph = nullptr;
        m_Registry.Clear();
        m_ModelCatalog.Clear();
        m_GPUDevice = nullptr;
    }

    void Update(const float deltaSeconds) override
    {
        if(!everify(State::Running == m_State, "Application is not running"))
        {
            return;
        }
        
        m_ScreenBounds = m_GPUDevice->GetExtent();

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

        // Transform to camera space and render
        for(const auto& cameraTuple : m_Registry.GetView<WorldMatrix, Camera>())
        {
            for(const auto& tuple : m_Registry.GetView<WorldMatrix, RefPtr<Model>>())
            {
                const auto [eid, worldMat, model] = tuple;
                m_RenderGraph->Add(worldMat, model);
            }

            const auto [camEid, camWorldMat, camera] = cameraTuple;
            auto renderResult = m_RenderGraph->Render(camWorldMat, camera.GetProjection());
            if (!renderResult)
            {
                logError(renderResult.error().Message);
            }

            m_RenderGraph->Reset();
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

        RefPtr<SDLGPUDevice> m_GPUDevice;
        SDLRenderGraph* m_RenderGraph = nullptr;
        EcsRegistry m_Registry;
        ModelCatalog m_ModelCatalog;
        WalkMouseNav m_WalkMouseNav{ TrsTransformf{}, 0.0001f, 5.0f };
        MouseNav* const m_MouseNav = &m_WalkMouseNav;
        EntityId m_EidCamera;
        EntityId m_EidModel;
        Extent m_ScreenBounds{0,0};
};

int main(int, [[maybe_unused]] char* argv[])
{
    SponzaApp app;
    AppDriver driver(&app);

    auto result = driver.Run();

    return 0;
}