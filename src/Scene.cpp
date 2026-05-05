#define __LOGGER_NAME__ "SCEN"

#include "Scene.h"

#include "Level.h"

#include "Stopwatch.h"

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
} // namespace

static size_t
CountModelInstances(const Level& level)
{
    size_t count = 0;
    for(const auto & handle : level.GetAllHandles())
    {
        auto node = level.GetNode(handle);
        MLG_ASSERT(node);

        if(node->ModelIndex.IsValid())
        {
            ++count;
        }
    }

    return count;
}

static size_t
CountWorldTransforms(const Level& level)
{
    return CountModelInstances(level);
}

static Result<WorldTransformBuffer>
BuildTransformBuffer(const Level& level,
    std::vector<Level::NodeHandle>& nodeHandles,
    wgpu::CommandEncoder encoder)
{
    // One world space transform per model instance.
    const size_t transformCount = CountWorldTransforms(level);

    nodeHandles.clear();
    nodeHandles.reserve(transformCount);

    const size_t sizeofBuffer = transformCount * sizeof(ShaderInterop::WorldTransform);

    auto buffer =
        WebgpuHelper::CreateSemanticStorageBuffer<WorldTransformBuffer>(sizeofBuffer, "TransformBuffer");
    MLG_CHECK(buffer);

    MLG_CHECK(buffer->Map());

    // Initialize the transform buffer with the world space transform
    // of each node that contains a model instance.

    size_t transformIndex = 0;

    for(auto& handle : level.GetAllHandles())
    {
        auto node = level.GetNode(handle);
        MLG_ASSERT(node);

        if(!node->ModelIndex.IsValid())
        {
            continue;
        }

        nodeHandles.push_back(handle);

        const ShaderInterop::WorldTransform transform{ .Transform = node->WorldTransform };

        buffer->Store(transformIndex, transform);
        ++transformIndex;
    }

    MLG_CHECK(buffer->Unmap(encoder));

    return buffer;
}

static Result<>
CollectModelInstances(const Level& level, std::vector<ModelInstance>& outModelInstances)
{
    const size_t modelCount = CountModelInstances(level);

    outModelInstances.clear();
    outModelInstances.reserve(modelCount);

    for(const auto& handle : level.GetAllHandles())
    {
        auto node = level.GetNode(handle);
        MLG_ASSERT(node);

        if(node->ModelIndex.IsValid())
        {
            const ModelInstance modelInstance{ .ModelIndex{ node->ModelIndex } };
            outModelInstances.push_back(modelInstance);
        }
    }

    return Result<>::Ok;
}

static inline size_t
CountMeshes(std::span<const Model> models, std::span<const ModelInstance> modelInstances)
{
    size_t meshCount = 0;
    for(const auto& modelInstance : modelInstances)
    {
        MLG_ASSERT(modelInstance.ModelIndex.Value() < models.size(),
            "Model instance has invalid model index: {} (model count: {})",
            modelInstance.ModelIndex.Value(),
            models.size());

        meshCount += models[modelInstance.ModelIndex.Value()].MeshCount;
    }
    return meshCount;
}

static Result<DrawIndirectBuffer>
BuildDrawIndirectBuffer(std::span<const ModelInstance> modelInstances,
    const PropKit& propKit,
    wgpu::CommandEncoder encoder)
{
    const std::span<const Mesh> meshes = propKit.GetMeshes();
    const std::span<const Model> models = propKit.GetModels();

    const size_t meshInstanceCount = CountMeshes(models, modelInstances);
    const size_t sizeofDrawIndirectBuffer =
        meshInstanceCount * sizeof(ShaderInterop::DrawIndirectParams);

    auto buffer = WebgpuHelper::CreateSemanticIndirectBuffer<DrawIndirectBuffer>(
        sizeofDrawIndirectBuffer,
        "DrawIndirectBuffer");
    MLG_CHECK(buffer);

    MLG_CHECK(buffer->Map());

    uint32_t meshCount = 0;

    for(const auto& modelInstance : modelInstances)
    {
        MLG_ASSERT(modelInstance.ModelIndex.Value() < models.size(),
            "Model instance has invalid model index: {} (model count: {})",
            modelInstance.ModelIndex.Value(), models.size());

        const Model& model = models[modelInstance.ModelIndex.Value()];

        MLG_ASSERT(model.FirstMesh.Value() + model.MeshCount <= meshes.size(),
            "Model has invalid mesh range: first mesh {}, mesh count {} (total meshes: {})",
            model.FirstMesh.Value(), model.MeshCount, meshes.size());

        const Mesh* meshSrc = &meshes[model.FirstMesh.Value()];

        for(uint32_t i = 0; i < model.MeshCount; ++i)
        {
            ShaderInterop::DrawIndirectParams drawParams //
                {
                    .IndexCount = meshSrc->IndexCount,
                    .InstanceCount = 1,
                    .FirstIndex = meshSrc->FirstIndex,
                    .BaseVertex = meshSrc->BaseVertex,
                    .FirstInstance = meshCount,
                };

            buffer->Store(meshCount, drawParams);

            ++meshCount;
            ++meshSrc;
        }
    }

    MLG_CHECK(buffer->Unmap(encoder));

    return buffer;
}

static Result<MeshPropertiesBuffer>
BuildMeshPropertiesBuffer(std::span<const ModelInstance> modelInstances,
    const PropKit& propKit,
    wgpu::CommandEncoder encoder)
{
    const std::span<const Mesh> meshes = propKit.GetMeshes();
    const std::span<const Model> models = propKit.GetModels();

    const size_t meshInstanceCount = CountMeshes(models, modelInstances);
    const size_t sizeofBuffer = meshInstanceCount * sizeof(ShaderInterop::MeshProperties);

    auto buffer = WebgpuHelper::CreateSemanticStorageBuffer<MeshPropertiesBuffer>(sizeofBuffer, "MeshPropertiesBuffer");
    MLG_CHECK(buffer);

    MLG_CHECK(buffer->Map());

    uint32_t meshCount = 0;
    uint32_t transformIndex = 0;

    for(const auto& modelInstance : modelInstances)
    {
        MLG_ASSERT(modelInstance.ModelIndex.Value() < models.size(),
            "Model instance has invalid model index: {} (model count: {})",
            modelInstance.ModelIndex.Value(), models.size());

        const Model& model = models[modelInstance.ModelIndex.Value()];

        MLG_ASSERT(model.FirstMesh.Value() + model.MeshCount <= meshes.size(),
            "Model has invalid mesh range: first mesh {}, mesh count {} (total meshes: {})",
            model.FirstMesh.Value(), model.MeshCount, meshes.size());

        const Mesh* meshSrc = &meshes[model.FirstMesh.Value()];

        for(uint32_t i = 0; i < model.MeshCount; ++i)
        {
            const BoundingSphere boundingSphere(meshSrc->BoundingBox);

            ShaderInterop::MeshProperties meshProps//
            {
                .Center = boundingSphere.GetCenter(),
                .Radius = boundingSphere.GetRadius(),
                .TransformIndex{ transformIndex },
                .MaterialIndex{ meshSrc->MaterialIndex.Value() },
            };

            buffer->Store(meshCount, meshProps);

            ++meshCount;
            ++meshSrc;
        }

        ++transformIndex;
    }

    MLG_CHECK(buffer->Unmap(encoder));

    return buffer;
}

static Result<wgpu::BindGroup>
CreateColorPipelineBindGroup0(ColorPipelineResources& colorPipelineResources)
{
    auto bgLayouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(bgLayouts);

    wgpu::BindGroupEntry bgEntries[] =//
    {
        {
            .binding = 0,
            .buffer = colorPipelineResources.WorldTransformBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorPipelineResources.WorldTransformBuffer.GetSize(),
        },
        {
            .binding = 1,
            .buffer = colorPipelineResources.ClipSpaceBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorPipelineResources.ClipSpaceBuffer.GetSize(),
        },
        {
            .binding = 2,
            .buffer = colorPipelineResources.MeshPropertiesBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorPipelineResources.MeshPropertiesBuffer.GetSize(),
        },
        {
            .binding = 3,
            .buffer = colorPipelineResources.MaterialConstantsBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorPipelineResources.MaterialConstantsBuffer.GetSize(),
        },
        {
            .binding = 4,
            .buffer = colorPipelineResources.CameraParamsBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorPipelineResources.CameraParamsBuffer.GetSize(),
        },
    };

    wgpu::BindGroupDescriptor bgDesc = //
        {
            .label = "ColorPipelineBindGroup0",
            .layout = (*bgLayouts)[0],
            .entryCount = std::size(bgEntries),
            .entries = bgEntries,
        };

    wgpu::BindGroup bindGroup = WebgpuHelper::GetDevice().CreateBindGroup(&bgDesc);
    MLG_CHECK(bindGroup,
        "Failed to create bind group 0 for color pipeline");

    return bindGroup;
}

static Result<wgpu::BindGroup>
CreateTransformPipelineBindGroup0(TransformPipelineResources& transformPipelineResources)
{
    auto bgLayouts = WebgpuHelper::GetTransformPipelineLayouts();
    MLG_CHECK(bgLayouts);

    wgpu::BindGroupEntry bgEntries[] =//
    {
        {
            .binding = 0,
            .buffer = transformPipelineResources.WorldTransformBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = transformPipelineResources.WorldTransformBuffer.GetSize(),
        },
        {
            .binding = 1,
            .buffer = transformPipelineResources.ClipSpaceBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = transformPipelineResources.ClipSpaceBuffer.GetSize(),
        },
        {
            .binding = 2,
            .buffer = transformPipelineResources.CameraParamsBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = transformPipelineResources.CameraParamsBuffer.GetSize(),
        },
    };

    wgpu::BindGroupDescriptor bgDesc = //
        {
            .label = "TransformPipelineBindGroup0",
            .layout = (*bgLayouts)[0],
            .entryCount = std::size(bgEntries),
            .entries = bgEntries,
        };

    wgpu::BindGroup bindGroup = WebgpuHelper::GetDevice().CreateBindGroup(&bgDesc);
    MLG_CHECK(bindGroup,
        "Failed to create bind group 0 for transform pipeline");

    return bindGroup;
}

Result<>
Scene::Create(const Level& level, const PropKit& propKit, Scene& outScene)
{
    Stopwatch createTimer;
    createTimer.Mark();

    wgpu::CommandEncoder encoder = WebgpuHelper::GetDevice().CreateCommandEncoder();

    std::vector<Level::NodeHandle> levelNodeHandles;

    auto transformBuffer = BuildTransformBuffer(level, levelNodeHandles, encoder);
    MLG_CHECK(transformBuffer);

    auto clipSpaceBuffer =
        WebgpuHelper::CreateSemanticStorageBuffer<ClipSpaceBuffer>(transformBuffer->GetSize(),
            "ClipSpaceBuffer");
    MLG_CHECK(clipSpaceBuffer);

    std::vector<ModelInstance> modelInstances;
    MLG_CHECK(CollectModelInstances(level, modelInstances));

    auto drawIndirectBuffer = BuildDrawIndirectBuffer(modelInstances, propKit, encoder);
    MLG_CHECK(drawIndirectBuffer);

    auto meshPropertiesBuffer = BuildMeshPropertiesBuffer(modelInstances, propKit, encoder);
    MLG_CHECK(meshPropertiesBuffer);

    auto cameraParamsBuf = WebgpuHelper::CreateSemanticUniformBuffer<CameraParamsBuffer>(
        sizeof(ShaderInterop::CameraParams),
        "CameraParamsBuffer");
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

    wgpu::CommandBuffer commandBuffer = encoder.Finish();
    WebgpuHelper::GetDevice().GetQueue().Submit(1, &commandBuffer);

    std::vector<Scene::TransformBufferOffset> transformBufferOffsets;
    transformBufferOffsets.reserve(levelNodeHandles.size());
    for(size_t i = 0; i < levelNodeHandles.size(); ++i)
    {
        const Scene::TransformBufferOffset offset //
            {
                .NodeHandle = levelNodeHandles[i],
                .Offset = i * sizeof(ShaderInterop::WorldTransform),
            };

        transformBufferOffsets.push_back(offset);
    }

    Scene scene(&propKit,
        *transformBuffer,
        *drawIndirectBuffer,
        *meshPropertiesBuffer,
        *cameraParamsBuf,
        *colorPipelineBindGroup0,
        *transformPipelineBindGroup0,
        std::move(modelInstances),
        std::move(transformBufferOffsets));

    outScene = std::move(scene);

    MLG_INFO("Scene created in {} ms", createTimer.ElapsedSeconds() * 1000);

    return Result<>::Ok;
}

Scene::Scene(const PropKit* propKit,
    WorldTransformBuffer worldTransformBuffer,
    DrawIndirectBuffer drawIndirectBuffer,
    MeshPropertiesBuffer meshPropertiesBuffer,
    CameraParamsBuffer cameraParamsBuffer,
    wgpu::BindGroup colorPipelineBindGroup0,
    wgpu::BindGroup transformPipelineBindGroup0,
    std::vector<ModelInstance>&& modelInstances,
    std::vector<TransformBufferOffset>&& transformBufferOffsets)
    : m_PropKit(propKit),
      m_WorldTransformBuffer(worldTransformBuffer),
      m_DrawIndirectBuffer(drawIndirectBuffer),
      m_MeshPropertiesBuffer(meshPropertiesBuffer),
      m_CameraParamsBuffer(cameraParamsBuffer),
      m_ColorPipelineBindGroup0(colorPipelineBindGroup0),
      m_TransformPipelineBindGroup0(transformPipelineBindGroup0),
      m_ModelInstances(std::move(modelInstances)),
      m_TransformBufferOffsets(std::move(transformBufferOffsets))
{
}

Result<>
Scene::BeginFrame()
{
    MLG_CHECKV(!m_FrameCommandEncoder, "BeginFrame called while a frame is already in progress");

    return BeginFrame(WebgpuHelper::GetDevice().CreateCommandEncoder());
}

Result<>
Scene::BeginFrame(wgpu::CommandEncoder cmdEncoder)
{
    MLG_CHECKV(!m_FrameCommandEncoder, "BeginFrame called while a frame is already in progress");

    m_FrameCommandEncoder = cmdEncoder;

    return m_WorldTransformBuffer.Map();
}

Result<>
Scene::UpdateWorldTransforms(const Level::NodeHandle nodeHandle, const Mat44f& worldTransform)
{
    MLG_CHECKV(m_FrameCommandEncoder, "UpdateWorldTransforms called without a matching BeginFrame");
    MLG_CHECKV(nodeHandle, "Invalid node handle");

    auto offset = GetTransformBufferOffset(nodeHandle);
    MLG_CHECK(offset);

    const ShaderInterop::WorldTransform transform{ .Transform = worldTransform };

    m_WorldTransformBuffer.Store(*offset / sizeof(transform), transform);

    return Result<>::Ok;
}

Result<>
Scene::EndFrame()
{
    MLG_CHECKV(m_FrameCommandEncoder, "EndFrame called without a matching BeginFrame");

    m_WorldTransformBuffer.Unmap(m_FrameCommandEncoder);

    wgpu::CommandBuffer commandBuffer = m_FrameCommandEncoder.Finish();
    WebgpuHelper::GetDevice().GetQueue().Submit(1, &commandBuffer);

    m_FrameCommandEncoder = nullptr;

    return Result<>::Ok;
}

// private:

Result<size_t>
Scene::GetTransformBufferOffset(const Level::NodeHandle& nodeHandle) const
{
    size_t offset = m_TransformBufferOffsets.size(); // Initialize to an invalid offset

    for (const auto& bufferOffset : m_TransformBufferOffsets)
    {
        if (bufferOffset.NodeHandle == nodeHandle)
        {
            offset = bufferOffset.Offset;
            break;
        }
    }

    MLG_CHECKV(offset != m_TransformBufferOffsets.size(),
        "Transform buffer offset not found for node handle");
    return offset;
}