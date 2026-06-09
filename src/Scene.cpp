#define MLG_LOGGER_NAME "SCEN"

#include "Scene.h"

#include "Level.h"
#include "narrow_cast.h"
#include "PropKit.h"
#include "Timer.h"

namespace
{
struct ColorShaderResources
{
    WorldTransformBuffer WorldTransformBuffer;
    ClipSpaceBuffer ClipSpaceBuffer;
    MaterialConstantsBuffer MaterialConstantsBuffer;
    MeshPropertiesBuffer MeshPropertiesBuffer;
    CameraParamsBuffer CameraParamsBuffer;
};

struct TransformShaderResources
{
    WorldTransformBuffer WorldTransformBuffer;
    ClipSpaceBuffer ClipSpaceBuffer;
    CameraParamsBuffer CameraParamsBuffer;
};

size_t
CountModelInstances(const Level& level)
{
    size_t count = 0;
    for(const Level::NodeHandle& handle : level.GetAllHandles())
    {
        const Level::Node* node = level.GetNode(handle);
        MLG_ASSERT(node);

        if(node->Components.Model.has_value())
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

    return WebgpuHelper::CreateStorageBuffer<WorldTransformBuffer>(outWorldTransforms, "TransformBuffer");
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
                    .IndexCount = meshSrc.IndexCount,
                    .InstanceCount = 1,
                    .FirstIndex = meshSrc.FirstIndex,
                    .BaseVertex = meshSrc.BaseVertex,
                    .FirstInstance = narrow_cast<uint32_t>(drawIndirectParams.size()),
                };

            drawIndirectParams.emplace_back(drawParams);
        }
    }

    return WebgpuHelper::CreateIndirectBuffer<DrawIndirectBuffer>(drawIndirectParams,
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
            const Sphere boundingSphere(meshSrc.BoundingBox);

            const ShaderInterop::MeshProperties meshProps//
            {
                .Radius = boundingSphere.GetRadius(),
                .TransformIndex = transformIndex,
                .MaterialIndex = narrow_cast<uint32_t>(meshSrc.MaterialId.GetValue()),
            };

            meshProperties.emplace_back(meshProps);
        }

        ++transformIndex;
    }

    return WebgpuHelper::CreateStorageBuffer<MeshPropertiesBuffer>(meshProperties,
        "MeshPropertiesBuffer");
}

Result<wgpu::BindGroup>
CreateColorShaderBindGroup(ColorShaderResources& colorShaderResources)
{
    auto bgLayouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(bgLayouts);

    const wgpu::BindGroupEntry bgEntries[] =//
    {
        {
            .binding = 0,
            .buffer = colorShaderResources.WorldTransformBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorShaderResources.WorldTransformBuffer.BufferSize(),
        },
        {
            .binding = 1,
            .buffer = colorShaderResources.ClipSpaceBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorShaderResources.ClipSpaceBuffer.BufferSize(),
        },
        {
            .binding = 2,
            .buffer = colorShaderResources.MeshPropertiesBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorShaderResources.MeshPropertiesBuffer.BufferSize(),
        },
        {
            .binding = 3,
            .buffer = colorShaderResources.MaterialConstantsBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorShaderResources.MaterialConstantsBuffer.BufferSize(),
        },
        {
            .binding = 4,
            .buffer = colorShaderResources.CameraParamsBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorShaderResources.CameraParamsBuffer.BufferSize(),
        },
    };

    const wgpu::BindGroupDescriptor bgDesc = //
        {
            .label = "ColorShaderBindGroup",
            .layout = (*bgLayouts)[0],
            .entryCount = std::size(bgEntries),
            .entries = &bgEntries[0],
        };

    wgpu::BindGroup bindGroup = WebgpuHelper::GetDevice().CreateBindGroup(&bgDesc);
    MLG_CHECK(bindGroup,
        "Failed to create bind group 0 for color shader");

    return bindGroup;
}

Result<wgpu::BindGroup>
CreateTransformShaderBindGroup(TransformShaderResources& transformShaderResources)
{
    auto bgLayouts = WebgpuHelper::GetTransformPipelineLayouts();
    MLG_CHECK(bgLayouts);

    const wgpu::BindGroupEntry bgEntries[] =//
    {
        {
            .binding = 0,
            .buffer = transformShaderResources.WorldTransformBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = transformShaderResources.WorldTransformBuffer.BufferSize(),
        },
        {
            .binding = 1,
            .buffer = transformShaderResources.ClipSpaceBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = transformShaderResources.ClipSpaceBuffer.BufferSize(),
        },
        {
            .binding = 2,
            .buffer = transformShaderResources.CameraParamsBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = transformShaderResources.CameraParamsBuffer.BufferSize(),
        },
    };

    const wgpu::BindGroupDescriptor bgDesc = //
        {
            .label = "TransformShaderBindGroup",
            .layout = (*bgLayouts)[0],
            .entryCount = std::size(bgEntries),
            .entries = &bgEntries[0],
        };

    wgpu::BindGroup bindGroup = WebgpuHelper::GetDevice().CreateBindGroup(&bgDesc);
    MLG_CHECK(bindGroup,
        "Failed to create bind group 0 for transform shader");

    return bindGroup;
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
        WebgpuHelper::CreateStorageBuffer<ClipSpaceBuffer>(transformBuffer->Count(),
            "ClipSpaceBuffer");
    MLG_CHECK(clipSpaceBuffer);

    auto drawIndirectBuffer = BuildDrawIndirectBuffer(modelInstances, propKit);
    MLG_CHECK(drawIndirectBuffer);

    auto meshPropertiesBuffer = BuildMeshPropertiesBuffer(modelInstances, propKit);
    MLG_CHECK(meshPropertiesBuffer);

    auto cameraParamsBuf =
        WebgpuHelper::CreateUniformBuffer<CameraParamsBuffer>(1, "CameraParamsBuffer");
    MLG_CHECK(cameraParamsBuf);

    ColorShaderResources colorShaderResources //
    {
        .WorldTransformBuffer = *transformBuffer,
        .ClipSpaceBuffer = *clipSpaceBuffer,
        .MaterialConstantsBuffer = propKit.GetMaterialConstantsBuffer(),
        .MeshPropertiesBuffer = *meshPropertiesBuffer,
        .CameraParamsBuffer = *cameraParamsBuf,
    };

    auto colorShaderBindGroup = CreateColorShaderBindGroup(colorShaderResources);
    MLG_CHECK(colorShaderBindGroup);

    TransformShaderResources transformShaderResources //
    {
        .WorldTransformBuffer = *transformBuffer,
        .ClipSpaceBuffer = *clipSpaceBuffer,
        .CameraParamsBuffer = *cameraParamsBuf,
    };

    auto transformShaderBindGroup = CreateTransformShaderBindGroup(transformShaderResources);
    MLG_CHECK(transformShaderBindGroup);

    Scene scene(std::move(*transformBuffer),
        std::move(*drawIndirectBuffer),
        std::move(*meshPropertiesBuffer),
        std::move(*cameraParamsBuf),
        std::move(*colorShaderBindGroup),
        std::move(*transformShaderBindGroup),
        std::move(modelInstances),
        std::move(worldTransforms),
        std::move(nodeHandles));

    MLG_INFO("Scene created in {} ms", createTimer.GetElapsedSeconds() * 1000);

    return std::move(scene);
}

Scene::Scene(WorldTransformBuffer worldTransformBuffer,
    DrawIndirectBuffer drawIndirectBuffer,
    MeshPropertiesBuffer meshPropertiesBuffer,
    CameraParamsBuffer cameraParamsBuffer,
    wgpu::BindGroup colorShaderBindGroup,
    wgpu::BindGroup transformShaderBindGroup,
    std::vector<ModelInstance> modelInstances,
    std::vector<ShaderInterop::WorldTransform> worldTransforms,
    std::vector<Level::NodeHandle> nodeHandles)
    : m_WorldTransformBuffer(std::move(worldTransformBuffer)),
      m_DrawIndirectBuffer(std::move(drawIndirectBuffer)),
      m_MeshPropertiesBuffer(std::move(meshPropertiesBuffer)),
      m_CameraParamsBuffer(std::move(cameraParamsBuffer)),
      m_ColorShaderBindGroup(std::move(colorShaderBindGroup)),
      m_TransformShaderBindGroup(std::move(transformShaderBindGroup)),
      m_ModelInstances(std::move(modelInstances)),
      m_WorldTransforms(std::move(worldTransforms)),
      m_NodeHandles(std::move(nodeHandles))
{
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
    WebgpuHelper::GetDevice().GetQueue().WriteBuffer(m_WorldTransformBuffer.GetGpuBuffer(),
        0,
        m_WorldTransforms.data(),
        m_WorldTransforms.size() * sizeof(m_WorldTransforms[0]));

    return Result<>::Ok;
}