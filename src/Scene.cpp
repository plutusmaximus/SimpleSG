#define MLG_LOGGER_NAME "SCEN"

#include "Scene.h"

#include "Level.h"
#include "narrow_cast.h"
#include "PropKit.h"
#include "Timer.h"

namespace
{
struct ColorPipelineResources
{
    WorldTransformBuffer WorldTransformBuffer;
    ClipSpaceBuffer ClipSpaceBuffer;
    MaterialConstantsBuffer MaterialConstantsBuffer;
    MeshPropertiesBuffer MeshPropertiesBuffer;
    CameraParamsBuffer CameraParamsBuffer;
};

struct TransformPipelineResources
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
CreateColorPipelineBindGroup0(ColorPipelineResources& colorPipelineResources)
{
    auto bgLayouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(bgLayouts);

    const wgpu::BindGroupEntry bgEntries[] =//
    {
        {
            .binding = 0,
            .buffer = colorPipelineResources.WorldTransformBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorPipelineResources.WorldTransformBuffer.BufferSize(),
        },
        {
            .binding = 1,
            .buffer = colorPipelineResources.ClipSpaceBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorPipelineResources.ClipSpaceBuffer.BufferSize(),
        },
        {
            .binding = 2,
            .buffer = colorPipelineResources.MeshPropertiesBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorPipelineResources.MeshPropertiesBuffer.BufferSize(),
        },
        {
            .binding = 3,
            .buffer = colorPipelineResources.MaterialConstantsBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorPipelineResources.MaterialConstantsBuffer.BufferSize(),
        },
        {
            .binding = 4,
            .buffer = colorPipelineResources.CameraParamsBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorPipelineResources.CameraParamsBuffer.BufferSize(),
        },
    };

    const wgpu::BindGroupDescriptor bgDesc = //
        {
            .label = "ColorPipelineBindGroup0",
            .layout = (*bgLayouts)[0],
            .entryCount = std::size(bgEntries),
            .entries = &bgEntries[0],
        };

    wgpu::BindGroup bindGroup = WebgpuHelper::GetDevice().CreateBindGroup(&bgDesc);
    MLG_CHECK(bindGroup,
        "Failed to create bind group 0 for color pipeline");

    return bindGroup;
}

Result<wgpu::BindGroup>
CreateTransformPipelineBindGroup0(TransformPipelineResources& transformPipelineResources)
{
    auto bgLayouts = WebgpuHelper::GetTransformPipelineLayouts();
    MLG_CHECK(bgLayouts);

    const wgpu::BindGroupEntry bgEntries[] =//
    {
        {
            .binding = 0,
            .buffer = transformPipelineResources.WorldTransformBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = transformPipelineResources.WorldTransformBuffer.BufferSize(),
        },
        {
            .binding = 1,
            .buffer = transformPipelineResources.ClipSpaceBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = transformPipelineResources.ClipSpaceBuffer.BufferSize(),
        },
        {
            .binding = 2,
            .buffer = transformPipelineResources.CameraParamsBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = transformPipelineResources.CameraParamsBuffer.BufferSize(),
        },
    };

    const wgpu::BindGroupDescriptor bgDesc = //
        {
            .label = "TransformPipelineBindGroup0",
            .layout = (*bgLayouts)[0],
            .entryCount = std::size(bgEntries),
            .entries = &bgEntries[0],
        };

    wgpu::BindGroup bindGroup = WebgpuHelper::GetDevice().CreateBindGroup(&bgDesc);
    MLG_CHECK(bindGroup,
        "Failed to create bind group 0 for transform pipeline");

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

    ColorPipelineResources colorPipelineResources //
    {
        .WorldTransformBuffer = *transformBuffer,
        .ClipSpaceBuffer = *clipSpaceBuffer,
        .MaterialConstantsBuffer = propKit.GetMaterialConstantsBuffer(),
        .MeshPropertiesBuffer = *meshPropertiesBuffer,
        .CameraParamsBuffer = *cameraParamsBuf,
    };

    auto colorPipelineBindGroup0 = CreateColorPipelineBindGroup0(colorPipelineResources);
    MLG_CHECK(colorPipelineBindGroup0);

    TransformPipelineResources transformPipelineResources //
    {
        .WorldTransformBuffer = *transformBuffer,
        .ClipSpaceBuffer = *clipSpaceBuffer,
        .CameraParamsBuffer = *cameraParamsBuf,
    };

    auto transformPipelineBindGroup0 = CreateTransformPipelineBindGroup0(transformPipelineResources);
    MLG_CHECK(transformPipelineBindGroup0);

    Scene scene(std::move(*transformBuffer),
        std::move(*drawIndirectBuffer),
        std::move(*meshPropertiesBuffer),
        std::move(*cameraParamsBuf),
        std::move(*colorPipelineBindGroup0),
        std::move(*transformPipelineBindGroup0),
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
    wgpu::BindGroup colorPipelineBindGroup0,
    wgpu::BindGroup transformPipelineBindGroup0,
    std::vector<ModelInstance> modelInstances,
    std::vector<ShaderInterop::WorldTransform> worldTransforms,
    std::vector<Level::NodeHandle> nodeHandles)
    : m_WorldTransformBuffer(std::move(worldTransformBuffer)),
      m_DrawIndirectBuffer(std::move(drawIndirectBuffer)),
      m_MeshPropertiesBuffer(std::move(meshPropertiesBuffer)),
      m_CameraParamsBuffer(std::move(cameraParamsBuffer)),
      m_ColorPipelineBindGroup0(std::move(colorPipelineBindGroup0)),
      m_TransformPipelineBindGroup0(std::move(transformPipelineBindGroup0)),
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