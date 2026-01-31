#include <SDL3/SDL.h>

#include "AppDriver.h"
#include "Application.h"
#include "Camera.h"
#include "GpuDevice.h"
#include "ResourceCache.h"
#include "scope_exit.h"

static Result<Model> CreateTriangleModel(ResourceCache* cache);

class TriangleApp : public Application
{
public:
    ~TriangleApp() override
    {
    }

    Result<void> Initialize(AppContext* context) override
    {
        auto cleanup = scope_exit([this]()
        {
            Shutdown();
        });

        m_GpuDevice = context->GpuDevice;
        m_ResourceCache = context->ResourceCache;

        auto renderGraphResult = m_GpuDevice->CreateRenderGraph();
        expect(renderGraphResult, renderGraphResult.error());

        m_RenderGraph = *renderGraphResult;

        m_ScreenBounds = m_GpuDevice->GetExtent();

        constexpr Radiansf fov = Radiansf::FromDegrees(45);

        m_CameraXform.T = Vec3f{ 0,0,-4 };
        m_Camera.SetPerspective(fov, m_ScreenBounds, 0.1f, 1000);

        auto modelResult = CreateTriangleModel(m_ResourceCache);
        expect(modelResult, modelResult.error());
        m_Model = *modelResult;

        m_IsRunning = true;

        cleanup.release();

        return {};
    }

    void Shutdown() override
    {
        m_IsRunning = false;

        if(m_RenderGraph)
        {
            m_GpuDevice->DestroyRenderGraph(m_RenderGraph);
        }
        m_GpuDevice = nullptr;
        m_RenderGraph = nullptr;
        m_ResourceCache = nullptr;
    }

    void Update(const float /*deltaSeconds*/) override
    {
        m_ScreenBounds = m_GpuDevice->GetExtent();

        m_Camera.SetBounds(m_ScreenBounds);

        TrsTransformf transform;

        // Transform to camera space and render
        m_RenderGraph->Add(transform.ToMatrix(), m_Model);
        auto renderResult = m_RenderGraph->Render(m_CameraXform.ToMatrix(), m_Camera.GetProjection());
        if(!renderResult)
        {
            logError(renderResult.error().GetMessage());
        }
    }

    bool IsRunning() const override
    {
        return m_IsRunning;
    }

private:

        GpuDevice* m_GpuDevice = nullptr;
        ResourceCache* m_ResourceCache = nullptr;
        RenderGraph* m_RenderGraph = nullptr;
        TrsTransformf m_CameraXform;
        Camera m_Camera;
        Extent m_ScreenBounds{0,0};
        Model m_Model;
        bool m_IsRunning = false;
};

class TriangleAppLifecycle : public AppLifecycle
{
public:
    Application* Create() override
    {
        return new TriangleApp();
    }

    void Destroy(Application* app) override
    {
        delete app;
    }

    std::string_view GetName() const override
    {
        return "Triangle";
    }
};

int main(int, char* /*argv[]*/)
{
    TriangleAppLifecycle appLifecycle;
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

// Triangle vertices
static const Vertex triangleVertices[] =
{
    {{0.0f, 0.5f,  0.0f}, {0.0f,  0.0f,  -1.0f},  {1, 1}}, // 0
    {{0.5f, 0.0f,  0.0f}, {0.0f,  0.0f,  -1.0f},  {0, 1}}, // 1
    {{-0.5f, 0.0f,  0.0f}, {0.0f,  0.0f,  -1.0f},  {0, 0}}, // 2
};

// Triangle indices
static const VertexIndex triangleIndices[] =
{
    0, 1, 2,
};

static Result<Model> CreateTriangleModel(ResourceCache* cache)
{
    imvector<MeshSpec>::builder meshSpecs =
    {
        {
            .Vertices{triangleVertices},
            .Indices{triangleIndices},
            .MtlSpec
            {
                .Color{"#FFA500"_rgba},
                .Albedo{TextureSpec::None},//{"images/Ant.png"},
                .VertexShader{"shaders/Debug/ColorVertexShader", 3},
                .FragmentShader{"shaders/Debug/ColorFragmentShader"}
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

    return cache->GetOrCreateModel(CacheKey("TriangleModel"), modelSpec);
}