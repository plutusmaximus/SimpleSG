#include <SDL3/SDL.h>

#include "AppDriver.h"
#include "Application.h"
#include "Camera.h"
#include "SDLGPUDevice.h"
#include "SDLRenderGraph.h"
#include "ResourceCache.h"

static Result<RefPtr<Model>> CreateTriangleModel(ResourceCache& cache);

class TriangleApp : public Application
{
public:
    ~TriangleApp() override
    {
        delete m_ResourceCache;
        m_ResourceCache = nullptr;
        delete m_RenderGraph;
        m_RenderGraph = nullptr;
    }

    std::string_view GetName() const override
    {
        return "Triangle";
    }

    Result<void> Initialize(RefPtr<SDLGPUDevice> gpuDevice) override
    {
        m_ResourceCache = new ResourceCache(gpuDevice);
        if(!m_ResourceCache)
        {
            Shutdown();
            return std::unexpected(Error("Failed to create ResourceCache"));
        }

        m_RenderGraph = new SDLRenderGraph(gpuDevice.Get());
        if(!m_RenderGraph)
        {
            Shutdown();
            return std::unexpected(Error("Failed to create SDLRenderGraph"));
        }

        m_GPUDevice = gpuDevice;
        m_ScreenBounds = m_GPUDevice->GetExtent();

        const Degreesf fov(45);
        
        m_CameraXform.T = Vec3f{ 0,0,-4 };
        m_Camera.SetPerspective(fov, m_ScreenBounds, 0.1f, 1000);

        auto modelResult = CreateTriangleModel(*m_ResourceCache);
        expect(modelResult, modelResult.error());
        m_Model = *modelResult;

        m_IsRunning = true;

        return {};
    }

    void Shutdown() override
    {
        delete m_ResourceCache;
        m_ResourceCache = nullptr;

        delete m_RenderGraph;
        m_RenderGraph = nullptr;

        m_GPUDevice = nullptr;
        m_IsRunning = false;
    }

    void Update(const float deltaSeconds) override
    {
        m_ScreenBounds = m_GPUDevice->GetExtent();

        m_Camera.SetBounds(m_ScreenBounds);

        TrsTransformf transform;

        // Transform to camera space and render
        m_RenderGraph->Add(transform.ToMatrix(), m_Model);
        auto renderResult = m_RenderGraph->Render(m_CameraXform.ToMatrix(), m_Camera.GetProjection());
        if(!renderResult)
        {
            logError(renderResult.error().Message);
        }
    }    

    bool IsRunning() const override
    {
        return m_IsRunning;
    }

private:

        RefPtr<SDLGPUDevice> m_GPUDevice;
        ResourceCache* m_ResourceCache = nullptr;
        SDLRenderGraph* m_RenderGraph = nullptr;
        TrsTransformf m_CameraXform;
        Camera m_Camera;
        Extent m_ScreenBounds{0,0};
        RefPtr<Model> m_Model;
        bool m_IsRunning = false;
};

int main(int, [[maybe_unused]] char* argv[])
{
    TriangleApp app;
    AppDriver driver(&app);

    auto initResult = driver.Init();
    if(!initResult)
    {
        logError(initResult.error().Message);
        return -1;
    }

    auto runResult = driver.Run();

    if(!runResult)
    {
        logError(runResult.error().Message);
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

static Result<RefPtr<Model>> CreateTriangleModel(ResourceCache& cache)
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

    return cache.GetOrCreateModel(CacheKey("TriangleModel"), modelSpec);
}