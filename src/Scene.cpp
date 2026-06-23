#define MLG_LOGGER_NAME "SCEN"

#include "Scene.h"

#include "Camera.h"
#include "GpuLayouts.h"
#include "Level.h"
#include "narrow_cast.h"
#include "PerfMetrics.h"
#include "PropKit.h"
#include "SceneTypes.h"
#include "shaders/ColorShaderContract.h"
#include "shaders/TransformShaderContract.h"
#include "Timer.h"

#include <ranges>

namespace
{

size_t
CountModelInstances(const Level& level)
{
    size_t count = 0;
    for(const Level::NodeHandle& handle : level.GetAllHandles())
    {
        const Level::Node* node = level.GetNode(handle);
        if(!MLG_VERIFY(node, "Invalid node handle: {}", handle.GetValue()))
        {
            continue;
        }

        if(node->Components.Model)
        {
            ++count;
        }
    }

    return count;
}

size_t
CountWorldTransforms(const Level& level)
{
    return CountModelInstances(level);
}

size_t
CountMeshInstances(const Level& level)
{
    size_t count = 0;

    for(const Level::NodeHandle& handle : level.GetAllHandles())
    {
        const Level::Node* node = level.GetNode(handle);
        MLG_ASSERT(node);

        const std::optional<const Model*> optModel = node->Components.Model;

        if(!optModel)
        {
            continue;
        }

        if(!MLG_VERIFY(*optModel, "Node has invalid model pointer"))
        {
            continue;
        }

        count += (*optModel)->GetMeshes().size();
    }

    return count;
}

Result<>
BuildScene(const Level& level,
    std::vector<Level::NodeHandle>& outNodeHandles,
    std::vector<ShaderInterop::WorldTransform>& outWorldTransforms,
    std::vector<ModelInstance>& outModelInstances,
    std::vector<MeshInstance>& outMeshInstances)
{
    // One world space transform per model instance.
    const size_t transformCount = CountWorldTransforms(level);

    const size_t meshInstanceCount = CountMeshInstances(level);

    outNodeHandles.clear();
    outWorldTransforms.clear();
    outModelInstances.clear();
    outMeshInstances.clear();

    outNodeHandles.reserve(transformCount);
    outWorldTransforms.reserve(transformCount);
    outModelInstances.reserve(transformCount);
    outMeshInstances.reserve(meshInstanceCount);

    // Initialize the transform buffer with the world space transform
    // of each node that contains a model instance.

    for(const Level::NodeHandle& handle : level.GetAllHandles())
    {
        const Level::Node* node = level.GetNode(handle);
        MLG_CHECKV(node, "Invalid node handle");

        const std::optional<const Model*> optModel = node->Components.Model;

        if(!optModel)
        {
            continue;
        }

        const Model* model = *optModel;
        MLG_CHECKV(model, "Node has invalid model pointer");

        outNodeHandles.emplace_back(handle);

        const ShaderInterop::WorldTransform worldTransform{ .Transform = node->WorldTransform };
        outWorldTransforms.emplace_back(worldTransform);

        const size_t meshInstanceOffset = outMeshInstances.size();
        const std::span<const Mesh> meshes = model->GetMeshes();

        for(const Mesh& mesh : meshes)
        {
            outMeshInstances.emplace_back(&mesh, outMeshInstances.size());
        }

        outModelInstances.emplace_back(model,
            std::span(outMeshInstances).subspan(meshInstanceOffset, meshes.size()));
    }
    
    return Result<>::Ok;
}

Result<DrawIndirectBuffer>
BuildDrawIndirectBuffer(std::span<const MeshInstance> meshInstances)
{
    std::vector<ShaderInterop::DrawIndirectParams> drawIndirectParams;
    drawIndirectParams.reserve(meshInstances.size());

    for(const MeshInstance& meshInstance : meshInstances)
    {
        const Mesh& meshSrc = meshInstance.GetMesh();

        const ShaderInterop::DrawIndirectParams drawParams //
            {
                .IndexCount = meshSrc.GetIndexCount(),
                .InstanceCount = 1,
                .FirstIndex = meshSrc.GetFirstIndex(),
                .BaseVertex = meshSrc.GetBaseVertex(),
                .FirstInstance = narrow_cast<uint32_t>(drawIndirectParams.size()),
            };

        drawIndirectParams.emplace_back(drawParams);
    }

    auto buffer = GpuHelper::CreateIndirectBuffer<DrawIndirectBuffer>(drawIndirectParams.size(),
        "DrawIndirectBuffer");
    MLG_CHECK(buffer);

    buffer->Store(drawIndirectParams);

    return buffer;
}

Result<MeshPropertiesBuffer>
BuildMeshPropertiesBuffer(const std::span<const ModelInstance> modelInstances,
    const std::span<const MeshInstance> meshInstances)
{
    const size_t meshInstanceCount = meshInstances.size();

    uint32_t transformIndex = 0;

    std::vector<ShaderInterop::MeshProperties> meshProperties;
    meshProperties.reserve(meshInstanceCount);

    for(const auto& modelInstance : modelInstances)
    {
        const Model* model = modelInstance.GetModel();
        MLG_CHECK(model);
        const std::span<const Mesh> meshes = model->GetMeshes();

        for(const auto& meshSrc : meshes)
        {
            const BoundingSphere boundingSphere(meshSrc.GetBoundingBox());

            const ShaderInterop::MeshProperties meshProps//
            {
                .Radius = boundingSphere.GetRadius(),
                .TransformIndex = transformIndex,
                // FIXME(KB) - reconcile material ID
                .MaterialIndex = narrow_cast<uint32_t>(meshSrc.GetMaterialId().GetValue()),
            };

            meshProperties.emplace_back(meshProps);
        }

        ++transformIndex;
    }

    auto buffer = GpuHelper::CreateStorageBuffer<MeshPropertiesBuffer>(meshProperties.size(),
        "MeshPropertiesBuffer");
    MLG_CHECK(buffer);

    buffer->Store(meshProperties);

    return buffer;
}
} // namespace

Result<Scene>
Scene::Create(const Level& level, const PropKit& propKit)
{
    Timer createTimer;
    createTimer.Start();

    std::vector<Level::NodeHandle> nodeHandles;
    std::vector<ShaderInterop::WorldTransform> worldTransforms;
    std::vector<ModelInstance> modelInstances;
    std::vector<MeshInstance> meshInstances;

    MLG_CHECK(BuildScene(level, nodeHandles, worldTransforms, modelInstances, meshInstances));

    auto transformBuffer =
        GpuHelper::CreateStorageBuffer<WorldTransformBuffer>(nodeHandles.size(), "WorldTransforms");
    MLG_CHECK(transformBuffer);

    transformBuffer->Store(worldTransforms);

    auto clipSpaceBuffer =
        GpuHelper::CreateStorageBuffer<ClipSpaceBuffer>(nodeHandles.size(), "ClipSpaceTransforms");
    MLG_CHECK(clipSpaceBuffer);

    auto drawIndirectBuffer = BuildDrawIndirectBuffer(meshInstances);
    MLG_CHECK(drawIndirectBuffer);

    auto meshPropertiesBuffer = BuildMeshPropertiesBuffer(modelInstances, meshInstances);
    MLG_CHECK(meshPropertiesBuffer);

    auto cameraParamsBuf =
        GpuHelper::CreateUniformBuffer<CameraParamsBuffer>(1, "CameraParams");
    MLG_CHECK(cameraParamsBuf);

    const ColorShaderContract::SceneGroup::Resources colorShaderResources //
        {
            .WorldTransforms = *transformBuffer,
            .ClipSpaceTransforms = *clipSpaceBuffer,
            .MeshProperties = *meshPropertiesBuffer,
            .MaterialConstants = propKit.GetMaterialConstants(),
            .CameraParams = *cameraParamsBuf,
        };

    auto colorShaderBindGroup =
        GpuLayouts::CreateBindGroup<ColorShaderContract::SceneGroup>(GpuHelper::GetDevice(),
            colorShaderResources);
    MLG_CHECK(colorShaderBindGroup);

    const TransformShaderContract::SceneGroup::Resources transformShaderResources //
        {
            .WorldTransforms = *transformBuffer,
            .ClipSpaceTransforms = *clipSpaceBuffer,
            .CameraParams = *cameraParamsBuf,
        };

    auto transformShaderBindGroup =
        GpuLayouts::CreateBindGroup<TransformShaderContract::SceneGroup>(GpuHelper::GetDevice(),
            transformShaderResources);
    MLG_CHECK(transformShaderBindGroup);

    Scene scene(std::move(*transformBuffer),
        std::move(*drawIndirectBuffer),
        std::move(*meshPropertiesBuffer),
        std::move(*cameraParamsBuf),
        std::move(*colorShaderBindGroup),
        std::move(*transformShaderBindGroup),
        std::move(nodeHandles),
        std::move(modelInstances),
        std::move(meshInstances),
        std::move(worldTransforms));

    MLG_INFO("Scene created in {} ms", createTimer.GetElapsedSeconds() * 1000);

    return std::move(scene);
}

Scene::Scene(WorldTransformBuffer worldTransformBuffer,
    DrawIndirectBuffer drawIndirectBuffer,
    MeshPropertiesBuffer meshPropertiesBuffer,
    CameraParamsBuffer cameraParamsBuffer,
    wgpu::BindGroup colorShaderBindGroup,
    wgpu::BindGroup transformShaderBindGroup,
    std::vector<Level::NodeHandle> nodeHandles,
    std::vector<ModelInstance> modelInstances,
    std::vector<MeshInstance> meshInstances,
    std::vector<ShaderInterop::WorldTransform> worldTransforms)
    : m_WorldTransformBuffer(std::move(worldTransformBuffer)),
      m_DrawIndirectBuffer(std::move(drawIndirectBuffer)),
      m_MeshPropertiesBuffer(std::move(meshPropertiesBuffer)),
      m_CameraParamsBuffer(std::move(cameraParamsBuffer)),
      m_ColorShaderBindGroup(std::move(colorShaderBindGroup)),
      m_TransformShaderBindGroup(std::move(transformShaderBindGroup)),
      m_NodeHandles(std::move(nodeHandles)),
      m_ModelInstances(std::move(modelInstances)),
      m_MeshInstances(std::move(meshInstances)),
      m_WorldTransforms(std::move(worldTransforms))
{
}

namespace
{
struct ModelCuller
{
    ModelCuller() = delete;

    explicit ModelCuller(const Frustum& frustum)
        : m_Frustum(frustum)
    {
    }

    template<typename T>
    bool operator()(const T& item) const
    {
        auto&& [modelInstance, worldXForm] = item;

        static_assert(std::same_as<std::remove_cvref_t<decltype(modelInstance)>, ModelInstance>,
            "ModelCuller expects element 0 to be ModelInstance");

        static_assert(
            std::same_as<std::remove_cvref_t<decltype(worldXForm)>, ShaderInterop::WorldTransform>,
            "ModelCuller expects element 1 to be ShaderInterop::WorldTransform");

        const BoundingSphere& boundingSphere = worldXForm.Transform * modelInstance.GetBoundingSphere();

        return modelInstance.IsVisible() && m_Frustum.Contains(boundingSphere);
    }

private:
    const Frustum& m_Frustum; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};

struct MeshExtractor
{
    template<typename T>
    auto operator()(const T& item) const
    {
        auto&& [modelInstance, worldXForm] = item;

        static_assert(std::same_as<std::remove_cvref_t<decltype(modelInstance)>, ModelInstance>,
            "MeshExtractor expects element 0 to be ModelInstance");

        static_assert(
            std::same_as<std::remove_cvref_t<decltype(worldXForm)>, ShaderInterop::WorldTransform>,
            "MeshExtractor expects element 1 to be ShaderInterop::WorldTransform");

        const std::span<const MeshInstance> meshInstances = modelInstance.GetMeshInstances();

        auto returnValue =
            meshInstances |
            std::views::transform(
                [&worldXForm](const MeshInstance& meshInstance)
                {
                    return std::tuple<const MeshInstance&, const ShaderInterop::WorldTransform&>(meshInstance,
                        worldXForm);
                });
                
        return returnValue;
    }
};
struct MeshCuller
{
    MeshCuller() = delete;

    explicit MeshCuller(const Frustum& frustum)
        : m_Frustum(frustum)
    {
    }

    template<typename T>
    bool operator()(const T& item) const
    {
        auto&& [meshInstance, worldXForm] = item;

        static_assert(std::same_as<std::remove_cvref_t<decltype(meshInstance)>, MeshInstance>,
            "MeshCuller expects element 0 to be MeshInstance");

        static_assert(
            std::same_as<std::remove_cvref_t<decltype(worldXForm)>, ShaderInterop::WorldTransform>,
            "MeshCuller expects element 1 to be ShaderInterop::WorldTransform");

        const BoundingSphere& boundingSphere = worldXForm.Transform * meshInstance.GetBoundingSphere();

        return m_Frustum.Contains(boundingSphere);
    }

private:
    const Frustum& m_Frustum; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
};
} // namespace

void
Scene::GetVisibleMeshes(const Frustum& frustum, std::vector<MeshInstance>& outVisibleMeshes) const
{
    static PerfCounter pcTotalMeshes({ .Name = "Scene.Meshes.Total",
        .Policy = PerfCounter::SamplePolicy::ResetOnSample });

    static PerfCounter pcVisibleMeshes({ .Name = "Scene.Meshes.Visible",
        .Policy = PerfCounter::SamplePolicy::ResetOnSample });

    pcTotalMeshes.Increment(m_MeshInstances.size());

    outVisibleMeshes.clear();

    auto cullPipeline = std::views::zip(m_ModelInstances, m_WorldTransforms) //
                        | std::views::filter(ModelCuller(frustum))           //
                        | std::views::transform(MeshExtractor())             //
                        | std::views::join                                   //
                        | std::views::filter(MeshCuller(frustum));

    for(const auto&& [meshInstance, worldXForm] : cullPipeline)
    {
        outVisibleMeshes.emplace_back(meshInstance);
    }

    pcVisibleMeshes.Increment(outVisibleMeshes.size());
}

Result<>
Scene::SyncFromLevel(const Level& level)
{
    for(size_t i = 0; i < m_NodeHandles.size(); ++i)
    {
        const Level::NodeHandle& nodeHandle = m_NodeHandles[i];
        const Level::Node* node = level.GetNode(nodeHandle);

        if(!MLG_VERIFY(node, "Node not found in level"))
        {
            continue;
        }

        const ShaderInterop::WorldTransform transform{ .Transform = node->WorldTransform };
        m_WorldTransforms[i] = transform;

        m_ModelInstances[i].SetVisible(level.IsVisible(nodeHandle));
    }

    return Result<>::Ok;
}

Result<>
Scene::SyncToGpu()
{
    // Brute force copy everything for now.
    GpuHelper::GetDevice().GetQueue().WriteBuffer(m_WorldTransformBuffer.GetGpuBuffer(),
        0,
        m_WorldTransforms.data(),
        m_WorldTransforms.size() * sizeof(m_WorldTransforms[0]));

    return Result<>::Ok;
}