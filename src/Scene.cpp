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

Result<WorldTransformBuffer>
BuildTransformBuffer(const Level& level,
    std::vector<Level::NodeHandle>& outNodeHandles,
    std::vector<ShaderInterop::WorldTransform>& outWorldTransforms,
    std::vector<ModelInstance>& outModelInstances)
{
    // One world space transform per model instance.
    const size_t transformCount = CountWorldTransforms(level);

    outNodeHandles.clear();
    outWorldTransforms.clear();
    outModelInstances.clear();

    outNodeHandles.reserve(transformCount);
    outWorldTransforms.reserve(transformCount);
    outModelInstances.reserve(transformCount);

    // Initialize the transform buffer with the world space transform
    // of each node that contains a model instance.

    for(const Level::NodeHandle& handle : level.GetAllHandles())
    {
        const Level::Node* node = level.GetNode(handle);
        MLG_ASSERT(node);

        const std::optional<ModelIdentifier>& optModelId = node->Components.Model;

        if(!optModelId)
        {
            continue;
        }

        MLG_CHECKV(optModelId->IsValid(), "Node has invalid model id");
        const ModelInstance modelInstance{*optModelId};
        outModelInstances.emplace_back(modelInstance);

        outNodeHandles.emplace_back(handle);

        const ShaderInterop::WorldTransform transform{ .Transform = node->WorldTransform };
        outWorldTransforms.emplace_back(transform);
    }

    return GpuHelper::CreateStorageBuffer<WorldTransformBuffer>(outWorldTransforms, "TransformBuffer");
}

Result<size_t>
CountMeshes(std::span<const ModelInstance> modelInstances, const PropKit& propKit)
{
    size_t meshCount = 0;
    for(const ModelInstance& modelInstance : modelInstances)
    {
        auto meshes = propKit.GetMeshes(modelInstance.GetModelId());
        MLG_CHECK(meshes);

        meshCount += meshes->size();
    }
    return meshCount;
}

Result<DrawIndirectBuffer>
BuildDrawIndirectBuffer(std::span<const ModelInstance> modelInstances, const PropKit& propKit)
{
    auto meshInstanceCount = CountMeshes(modelInstances, propKit);
    MLG_CHECK(meshInstanceCount);

    std::vector<ShaderInterop::DrawIndirectParams> drawIndirectParams;
    drawIndirectParams.reserve(*meshInstanceCount);

    for(const ModelInstance& modelInstance : modelInstances)
    {
        auto meshes = propKit.GetMeshes(modelInstance.GetModelId());
        MLG_CHECK(meshes);

        for(const Mesh& meshSrc : *meshes)
        {
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
    }

    return GpuHelper::CreateIndirectBuffer<DrawIndirectBuffer>(drawIndirectParams,
        "DrawIndirectBuffer");
}

Result<MeshPropertiesBuffer>
BuildMeshPropertiesBuffer(std::span<const ModelInstance> modelInstances, const PropKit& propKit)
{
    auto meshInstanceCount = CountMeshes(modelInstances, propKit);
    MLG_CHECK(meshInstanceCount);

    uint32_t transformIndex = 0;

    std::vector<ShaderInterop::MeshProperties> meshProperties;
    meshProperties.reserve(*meshInstanceCount);

    for(const auto& modelInstance : modelInstances)
    {
        auto meshes = propKit.GetMeshes(modelInstance.GetModelId());
        MLG_CHECK(meshes);

        for(const auto& meshSrc : *meshes)
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

    return GpuHelper::CreateStorageBuffer<MeshPropertiesBuffer>(meshProperties,
        "MeshPropertiesBuffer");
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

    auto transformBuffer = BuildTransformBuffer(level, nodeHandles, worldTransforms, modelInstances);
    MLG_CHECK(transformBuffer);

    auto clipSpaceBuffer =
        GpuHelper::CreateStorageBuffer<ClipSpaceBuffer>(transformBuffer->Count(),
            "ClipSpaceBuffer");
    MLG_CHECK(clipSpaceBuffer);

    auto drawIndirectBuffer = BuildDrawIndirectBuffer(modelInstances, propKit);
    MLG_CHECK(drawIndirectBuffer);

    auto meshPropertiesBuffer = BuildMeshPropertiesBuffer(modelInstances, propKit);
    MLG_CHECK(meshPropertiesBuffer);

    auto cameraParamsBuf =
        GpuHelper::CreateUniformBuffer<CameraParamsBuffer>(1, "CameraParamsBuffer");
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
    std::vector<ShaderInterop::WorldTransform> worldTransforms)
    : m_WorldTransformBuffer(std::move(worldTransformBuffer)),
      m_DrawIndirectBuffer(std::move(drawIndirectBuffer)),
      m_MeshPropertiesBuffer(std::move(meshPropertiesBuffer)),
      m_CameraParamsBuffer(std::move(cameraParamsBuffer)),
      m_ColorShaderBindGroup(std::move(colorShaderBindGroup)),
      m_TransformShaderBindGroup(std::move(transformShaderBindGroup)),
      m_NodeHandles(std::move(nodeHandles)),
      m_ModelInstances(std::move(modelInstances)),
      m_WorldTransforms(std::move(worldTransforms))
{
}

void
Scene::GetVisibleMeshes(const Frustum& frustum,
    const PropKit& propKit,
    std::vector<MeshInstance>& outVisibleMeshes) const
{
    static PerfCounter pcTotalMeshes({ .Name = "Scene.Meshes.Total",
        .Policy = PerfCounter::SamplePolicy::ResetOnSample });

    static PerfCounter pcVisibleMeshes({ .Name = "Scene.Meshes.Visible",
        .Policy = PerfCounter::SamplePolicy::ResetOnSample });

    outVisibleMeshes.clear();

    const auto view = std::views::zip(m_ModelInstances, m_WorldTransforms);

    size_t meshIndex = 0;

    for(const auto&& [modelInstance, worldXForm] : view)
    {
        const ModelIdentifier modelId = modelInstance.GetModelId();

        const Model* model = propKit.GetModel(modelId);
        if(!MLG_VERIFY(model, "Failed to get model for model id: {}", modelId.GetValue()))
        {
            continue;
        }

        auto meshes = propKit.GetMeshes(modelId);
        if(!MLG_VERIFY(meshes, "Failed to get meshes for model id: {}", modelId.GetValue()))
        {
            continue;
        }

        pcTotalMeshes.Increment(meshes->size());

        const Vec4f& pos4 = worldXForm.Transform[3];
        const Vec3f pos = Vec3f(pos4.x, pos4.y, pos4.z);

        if(!modelInstance.IsVisible() || !frustum.Contains(model->GetBoundingSphere() + pos))
        {
            meshIndex += meshes->size();
            continue;
        }

        for(const Mesh& mesh : *meshes)
        {
            if(frustum.Contains(mesh.GetBoundingSphere() + pos))
            {
                pcVisibleMeshes.Increment(1);

                outVisibleMeshes.emplace_back(&mesh, meshIndex);
            }

            ++meshIndex;
        }
    }
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