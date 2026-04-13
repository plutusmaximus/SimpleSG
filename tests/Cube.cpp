#include "AppDriver.h"
#include "Application.h"
#include "Camera.h"
#include "DawnSceneKit.h"
#include "ECS.h"
#include "EcsChildTransformPool.h"
#include "DawnGpuDevice.h"
#include "ImGuiRenderer.h"
#include "Log.h"
#include "MouseNav.h"
#include "RenderCompositor.h"
#include "Renderer.h"
#include "Shapes.h"

#include "scope_exit.h"

#include <filesystem>
#include <SDL3/SDL.h>

namespace
{
static Result<SceneKitSourceData> CreateShapeModel();

class WorldMatrix : public Mat44f
{
public:
    WorldMatrix& operator=(const Mat44& that)
    {
        this->Mat44f::operator=(that);
        return *this;
    }
};

class CubeApp : public Application
{
public:
    ~CubeApp() override
    {
    }

    Result<> Initialize(AppContext* context) override
    {
        auto cleanup = scope_exit([this]()
        {
            Shutdown();
        });

        MLG_INFO("Initializing...");

        MLG_CHECKV(State::None == m_State, "Application already initialized or running");

        m_State = State::Initialized;

        m_GpuDevice = context->GpuDevice;

        m_Renderer = m_GpuDevice->GetRenderer();
        m_RenderCompositor = m_GpuDevice->GetRenderCompositor();
        m_ImGuiRenderer = new ImGuiRenderer(m_GpuDevice);

        m_ScreenBounds = m_GpuDevice->GetScreenBounds();
        m_Planet = m_Registry.CreateEntity();
        m_MoonOrbit = m_Registry.CreateEntity();
        m_Moon = m_Registry.CreateEntity();
        m_Camera = m_Registry.CreateEntity();

        auto sceneKitData = CreateShapeModel();
        MLG_CHECK(sceneKitData);

        wgpu::Device wgpuDevice = static_cast<DawnGpuDevice*>(m_GpuDevice)->Device;
        std::filesystem::path rootPath = ".";
        auto dawnSceneKit = DawnSceneKit::Create(wgpuDevice, rootPath, *sceneKitData);
        MLG_CHECK(dawnSceneKit);

        SceneKit* sceneKit = *dawnSceneKit;

        constexpr Radiansf fov = Radiansf::FromDegrees(45);

        m_Planet.Add(ChildTransform{}, WorldMatrix{}, sceneKit);
        m_MoonOrbit.Add(ChildTransform{ .ParentId = m_Planet.GetId() }, WorldMatrix{});
        m_Moon.Add(ChildTransform{ .ParentId = m_MoonOrbit.GetId() }, WorldMatrix{}, sceneKit);
        m_Camera.Add(TrsTransformf{}, WorldMatrix{}, Camera{});

        m_Camera.Get<TrsTransformf>().T = Vec3f{ 0,0,-4 };
        m_Camera.Get<Camera>().SetPerspective(fov, m_ScreenBounds, 0.1f, 1000);

        m_GimbleMouseNav.SetTransform(m_Camera.Get<TrsTransformf>());

        m_State = State::Running;

        cleanup.release();

        return Result<>::Ok;
    }

    void Shutdown() override
    {
        MLG_INFO("Shutting down...");

        if(State::Shutdown == m_State)
        {
            return;
        }
        m_State = State::Shutdown;

        m_Registry.Clear();

        delete m_ImGuiRenderer;

        m_GpuDevice = nullptr;
        m_Renderer = nullptr;
    }

    void Update(const float deltaSeconds) override
    {
        if(!MLG_VERIFY(State::Running == m_State, "Application is not running"))
        {
            return;
        }

        m_ScreenBounds = m_GpuDevice->GetScreenBounds();

        m_Camera.Get<Camera>().SetBounds(m_ScreenBounds);

        m_MouseNav->Update(deltaSeconds);
        m_Camera.Get<TrsTransformf>() = m_MouseNav->GetTransform();

        // Update model matrix
        m_PlanetSpinAngle += 0.001f;
        m_MoonSpinAngle += 0.005f;
        m_MoonOrbitAngle += 0.005f;

        const Quatf planetTilt{ Radiansf::FromDegrees(15), Vec3f::ZAXIS() };

        auto& planetXform = m_Planet.Get<ChildTransform>();
        planetXform.LocalTransform.R = planetTilt * Quatf{ m_PlanetSpinAngle, Vec3f::YAXIS() };
        auto& moonOrbitXform = m_MoonOrbit.Get<ChildTransform>();
        moonOrbitXform.LocalTransform.R = Quatf{ m_MoonOrbitAngle, Vec3f::YAXIS() };

        auto& moonXform = m_Moon.Get<ChildTransform>();
        moonXform.LocalTransform.T = Vec3f{ 0,0,-2 };
        moonXform.LocalTransform.R = Quatf{ m_MoonSpinAngle, Vec3f::YAXIS() };
        moonXform.LocalTransform.S = Vec3f{ 0.25f };

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

    GpuDevice* m_GpuDevice = nullptr;
    RenderCompositor* m_RenderCompositor = nullptr;
    Renderer* m_Renderer = nullptr;
    ImGuiRenderer* m_ImGuiRenderer = nullptr;
    EcsRegistry m_Registry;
    GimbleMouseNav m_GimbleMouseNav{ TrsTransformf{}};
    MouseNav* const m_MouseNav = &m_GimbleMouseNav;
    Entity m_Camera;
    Entity m_Planet;
    Entity m_MoonOrbit;
    Entity m_Moon;
    Extent m_ScreenBounds{0,0};
    Radiansf m_PlanetSpinAngle{0}, m_MoonSpinAngle{0}, m_MoonOrbitAngle{0};
};

class CubeAppLifecycle : public AppLifecycle
{
public:
    Application* Create() override
    {
        return new CubeApp();
    }

    void Destroy(Application* app) override
    {
        delete app;
    }

    std::string_view GetName() const override
    {
        return "Cube";
    }
};

// Cube vertices (8 corners with positions, normals, and colors)
constexpr static const Vertex cubeVertices[] =
{
    // Front face
    {{-0.5f, -0.5f,  0.5f}, {0.0f,  0.0f,  1.0f},  {1, 1}}, // 0
    {{ 0.5f, -0.5f,  0.5f}, {0.0f,  0.0f,  1.0f},  {0, 1}}, // 1
    {{ 0.5f,  0.5f,  0.5f}, {0.0f,  0.0f,  1.0f},  {0, 0}}, // 2
    {{-0.5f,  0.5f,  0.5f}, {0.0f,  0.0f,  1.0f},  {1, 0}}, // 3
    // Back face
    {{-0.5f, -0.5f, -0.5f}, {0.0f,  0.0f, -1.0f},  {0, 1}}, // 4
    {{ 0.5f, -0.5f, -0.5f}, {0.0f,  0.0f, -1.0f},  {1, 1}}, // 5
    {{ 0.5f,  0.5f, -0.5f}, {0.0f,  0.0f, -1.0f},  {1, 0}}, // 6
    {{-0.5f,  0.5f, -0.5f}, {0.0f,  0.0f, -1.0f},  {0, 0}}, // 7
    // Left face
    {{-0.5f, -0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f},  {1, 1}}, // 8
    {{-0.5f, -0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f},  {0, 1}}, // 9
    {{-0.5f,  0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f},  {0, 0}}, // 10
    {{-0.5f,  0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f},  {1, 0}}, // 11
    // Right face
    {{0.5f, -0.5f,  0.5f}, {1.0f,  0.0f,  0.0f},  {1, 1}}, // 12
    {{0.5f, -0.5f, -0.5f}, {1.0f,  0.0f,  0.0f},  {0, 1}}, // 13
    {{0.5f,  0.5f, -0.5f}, {1.0f,  0.0f,  0.0f},  {0, 0}}, // 14
    {{0.5f,  0.5f,  0.5f}, {1.0f,  0.0f,  0.0f},  {1, 0}}, // 15
    // Top face
    {{-0.5f,  0.5f,  0.5f}, {0.0f,  1.0f,  0.0f},  {0, 0}}, // 16
    {{ 0.5f,  0.5f,  0.5f}, {0.0f,  1.0f,  0.0f},  {1, 0}}, // 17
    {{ 0.5f,  0.5f, -0.5f}, {0.0f,  1.0f,  0.0f},  {1, 1}}, // 18
    {{-0.5f,  0.5f, -0.5f}, {0.0f,  1.0f,  0.0f},  {0, 1}}, // 19
    // Bottom face
    {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f,  0.0f},  {0, 1}}, // 20
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f,  0.0f},  {1, 1}}, // 21
    {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f,  0.0f},  {1, 0}}, // 22
    {{-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f,  0.0f},  {0, 0}}, // 23
};

constexpr static const VertexIndex cubeIndices[] =
{
    // Front
    0, 2, 3,  0, 1, 2,
    // Back
    5, 7, 6,  5, 4, 7,
    // Left
    11, 9, 10,  8, 9, 11,
    // Right
    15, 13, 14,  12, 13, 15,
    // Top
    18, 16, 17,  19, 16, 18,
    // Bottom
    20, 22, 23,  20, 21, 22
};

static Result<SceneKitSourceData> CreateShapeModel()
{
    //auto geometry = Shapes::Box(1, 1, 1);
    //auto geometry = Shapes::Ball(1, 10);
    //auto geometry = Shapes::Cylinder(1, 1, 10);
    //auto geometry = Shapes::Cone(1, 0.5f, 10);
    auto geometry = Shapes::Torus(1, 0.5, 5);
    const auto &[vertices, indices] = geometry;

    SceneKitSourceData sceneKitData;

    const MaterialData mtlData //
    {
        .BaseTextureUri = "images/Ant.png",
        .Color = {1, 0, 0},
        .Metalness = 0,
        .Roughness = 0
    };

    const TransformData transformData //
        {
            .Transform = Mat44f(1),
            .ParentIndex = TransformData::kInvalidParentIndex,
        };

    const MeshData meshData //
    {
        .FirstIndex = 0,
        .IndexCount = static_cast<uint32_t>(indices.size()),
        .BaseVertex = 0,
        .MaterialIndex = 0,
    };

    const ModelInstance modelInstance //
    {
        .FirstMesh = 0,
        .MeshCount = 1,
        .TransformIndex = 0,
    };

    sceneKitData.Vertices.assign(vertices.begin(), vertices.end());
    sceneKitData.Indices.assign(indices.begin(), indices.end());
    sceneKitData.Materials.emplace_back(mtlData);
    sceneKitData.Transforms.emplace_back(transformData);
    sceneKitData.Meshes.emplace_back(meshData);
    sceneKitData.ModelInstances.emplace_back(modelInstance);

    return std::move(sceneKitData);
}
}

int main(int, char* /*argv*/[])
{
    CubeAppLifecycle appLifecycle;
    AppDriver driver(&appLifecycle);

    auto initResult = driver.Init();
    if(!initResult)
    {
        return -1;
    }

    auto runResult = driver.Run();

    if(!runResult)
    {
        return -1;
    }

    return 0;
}