#include <SDL3/SDL.h>

#include "AppDriver.h"
#include "Application.h"
#include "Camera.h"
#include "ECS.h"
#include "EcsChildTransformPool.h"
#include "ResourceCache.h"
#include "MouseNav.h"
#include "GpuDevice.h"
#include "Shapes.h"

#include "scope_exit.h"

[[maybe_unused]] static Result<Model> CreateCubeModel(ResourceCache* cache);
static Result<Model> CreateShapeModel(ResourceCache* cache);

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

    Result<void> Initialize(AppContext* context) override
    {
        auto cleanup = scope_exit([this]()
        {
            Shutdown();
        });

        logInfo("Initializing...");

        if(!everify(State::None == m_State, "Application already initialized or running"))
        {
            return Error("Application already initialized or running");
        }

        m_State = State::Initialized;

        m_GpuDevice = context->GpuDevice;
        m_ResourceCache = context->ResourceCache;

        auto renderGraphResult = m_GpuDevice->CreateRenderGraph();
        expect(renderGraphResult, renderGraphResult.error());

        m_RenderGraph = *renderGraphResult;

        m_ScreenBounds = m_GpuDevice->GetExtent();
        m_EidPlanet = m_Registry.Create();
        m_EidMoonOrbit = m_Registry.Create();
        m_EidMoon = m_Registry.Create();
        m_EidCamera = m_Registry.Create();

        //auto modelResult = CreateCube(gd);
        //auto modelResult = CreatePumpkin(gd);
        auto modelResult = CreateShapeModel(m_ResourceCache);
        expect(modelResult, modelResult.error());
        auto model = modelResult.value();

        constexpr Radiansf fov = Radiansf::FromDegrees(45);

        m_Registry.Add(m_EidPlanet, ChildTransform{}, WorldMatrix{}, model);
        m_Registry.Add(m_EidMoonOrbit, ChildTransform{ .ParentId = m_EidPlanet }, WorldMatrix{});
        m_Registry.Add(m_EidMoon, ChildTransform{ .ParentId = m_EidMoonOrbit }, WorldMatrix{}, model);
        m_Registry.Add(m_EidCamera, TrsTransformf{}, WorldMatrix{}, Camera{});

        m_Registry.Get<TrsTransformf>(m_EidCamera).T = Vec3f{ 0,0,-4 };
        m_Registry.Get<Camera>(m_EidCamera).SetPerspective(fov, m_ScreenBounds, 0.1f, 1000);

        m_GimbleMouseNav.SetTransform(m_Registry.Get<TrsTransformf>(m_EidCamera));

        m_State = State::Running;

        cleanup.release();

        return {};
    }

    void Shutdown() override
    {
        logInfo("Shutting down...");

        if(State::Shutdown == m_State)
        {
            return;
        }
        m_State = State::Shutdown;

        m_Registry.Clear();

        if(m_RenderGraph)
        {
            m_GpuDevice->DestroyRenderGraph(m_RenderGraph);
        }
        m_GpuDevice = nullptr;
        m_RenderGraph = nullptr;
        m_ResourceCache = nullptr;
    }

    void Update(const float deltaSeconds) override
    {
        if(!everify(State::Running == m_State, "Application is not running"))
        {
            return;
        }

        m_ScreenBounds = m_GpuDevice->GetExtent();

        m_Registry.Get<Camera>(m_EidCamera).SetBounds(m_ScreenBounds);

        m_MouseNav->Update(deltaSeconds);
        m_Registry.Get<TrsTransformf>(m_EidCamera) = m_MouseNav->GetTransform();

        // Update model matrix
        m_PlanetSpinAngle += 0.001f;
        m_MoonSpinAngle += 0.005f;
        m_MoonOrbitAngle += 0.005f;

        const Quatf planetTilt{ Radiansf::FromDegrees(15), Vec3f::ZAXIS() };

        auto& planetXform = m_Registry.Get<ChildTransform>(m_EidPlanet);
        planetXform.LocalTransform.R = planetTilt * Quatf{ m_PlanetSpinAngle, Vec3f::YAXIS() };
        auto& moonOrbitXform = m_Registry.Get<ChildTransform>(m_EidMoonOrbit);
        moonOrbitXform.LocalTransform.R = Quatf{ m_MoonOrbitAngle, Vec3f::YAXIS() };

        auto& moonXform = m_Registry.Get<ChildTransform>(m_EidMoon);
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

        // Transform to camera space and render
        for(const auto& cameraTuple : m_Registry.GetView<WorldMatrix, Camera>())
        {
            for(const auto& tuple : m_Registry.GetView<WorldMatrix, Model>())
            {
                const auto [eid, worldMat, model] = tuple;
                m_RenderGraph->Add(worldMat, model);
            }

            const auto [camEid, camWorldMat, camera] = cameraTuple;
            auto renderResult = m_RenderGraph->Render(camWorldMat, camera.GetProjection());
            if (!renderResult)
            {
                logError(renderResult.error().GetMessage());
            }
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
    RenderGraph* m_RenderGraph = nullptr;
    EcsRegistry m_Registry;
    GimbleMouseNav m_GimbleMouseNav{ TrsTransformf{}};
    MouseNav* const m_MouseNav = &m_GimbleMouseNav;
    EntityId m_EidCamera;
    EntityId m_EidPlanet;
    EntityId m_EidMoonOrbit;
    EntityId m_EidMoon;
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

int main(int, char* /*argv*/[])
{
    CubeAppLifecycle appLifecycle;
    AppDriver driver(&appLifecycle);

    auto initResult = driver.Init();
    if(!initResult)
    {
        logError(initResult.error().GetMessage());
        return -1;
    }

    auto runResult = driver.Run();

    if(!runResult)
    {
        logError(runResult.error().GetMessage());
        return -1;
    }

    return 0;
}

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

static Result<Model> CreateCubeModel(ResourceCache* cache)
{
    imvector<MeshSpec>::builder meshSpecs =
    {
        {
            .Vertices{std::span(cubeVertices + 0, 6)},
            .Indices{std::span(cubeIndices + 0, 6)},
            .MtlSpec
            {
                .Color{1, 0, 0},
                .Albedo{"images/Ant.png"},
                .VertexShader{"shaders/Debug/VertexShader", 3},
                .FragmentShader{"shaders/Debug/FragmentShader"},
            }
        },
        {
            .Vertices{std::span(cubeVertices + 6, 6)},
            .Indices{std::span(cubeIndices + 6, 6)},
            .MtlSpec
            {
                .Color{0, 1, 0},
                .Albedo{"images/Bee.png"},
                .VertexShader{"shaders/Debug/VertexShader", 3},
                .FragmentShader{"shaders/Debug/FragmentShader"},
            }
        },
        {
            .Vertices{std::span(cubeVertices + 12, 6)},
            .Indices{std::span(cubeIndices + 12, 6)},
            .MtlSpec
            {
                .Color{0, 0, 1},
                .Albedo{"images/Butterfly.png"},
                .VertexShader{"shaders/Debug/VertexShader", 3},
                .FragmentShader{"shaders/Debug/FragmentShader"},
            }
        },
        {
            .Vertices{std::span(cubeVertices + 18, 6)},
            .Indices{std::span(cubeIndices + 18, 6)},
            .MtlSpec
            {
                .Color{1, 1, 1},
                .Albedo{"images/Frog.png"},
                .VertexShader{"shaders/Debug/VertexShader", 3},
                .FragmentShader{"shaders/Debug/FragmentShader"},
            }
        },
        {
            .Vertices{std::span(cubeVertices + 24, 6)},
            .Indices{std::span(cubeIndices + 24, 6)},
            .MtlSpec
            {
                .Color{0, 1, 1},
                .Albedo{"images/Lizard.png"},
                .VertexShader{"shaders/Debug/VertexShader", 3},
                .FragmentShader{"shaders/Debug/FragmentShader"},
            }
        },
        {
            .Vertices{std::span(cubeVertices + 30, 6)},
            .Indices{std::span(cubeIndices + 30, 6)},
            .MtlSpec
            {
                .Color{1, 0, 1},
                .Albedo{"images/Turtle.png"},
                .VertexShader{"shaders/Debug/VertexShader", 3},
                .FragmentShader{"shaders/Debug/FragmentShader"},
            }
        },
    };

    imvector<TransformNode>::builder transformNodes
    {
        { .ParentIndex = -1 },
    };

    imvector<MeshInstance>::builder meshInstances
    {
        { .MeshIndex = 0, .NodeIndex = 0 },
        { .MeshIndex = 1, .NodeIndex = 0 },
        { .MeshIndex = 2, .NodeIndex = 0 },
        { .MeshIndex = 3, .NodeIndex = 0 },
        { .MeshIndex = 4, .NodeIndex = 0 },
        { .MeshIndex = 5, .NodeIndex = 0 },
    };

    const ModelSpec modelSpec{meshSpecs.build(), meshInstances.build(), transformNodes.build()};

    return cache->GetOrCreateModel(CacheKey("CubeModel"), modelSpec);
}

static Result<Model> CreateShapeModel(ResourceCache* cache)
{
    //auto geometry = Shapes::Box(1, 1, 1);
    //auto geometry = Shapes::Ball(1, 10);
    //auto geometry = Shapes::Cylinder(1, 1, 10);
    //auto geometry = Shapes::Cone(1, 0.5f, 10);
    auto geometry = Shapes::Torus(1, 0.5, 5);
    const auto& [vertices, indices] = geometry;

    imvector<MeshSpec>::builder meshSpecs =
    {
        {
            .Vertices{vertices},
            .Indices{indices},
            .MtlSpec
            {
                .Color{1, 0, 0},
                .Albedo{"images/Ant.png"},
                .VertexShader{"shaders/Debug/VertexShader", 3},
                .FragmentShader{"shaders/Debug/FragmentShader"},
            }
        }
    };

    imvector<TransformNode>::builder transformNodes
    {
        { .ParentIndex = -1 },
    };

    imvector<MeshInstance>::builder meshInstances
    {
        { .MeshIndex = 0, .NodeIndex = 0 }
    };

    const ModelSpec modelSpec{meshSpecs.build(), meshInstances.build(), transformNodes.build()};

    return cache->GetOrCreateModel(CacheKey("ShapeModel"), modelSpec);
}