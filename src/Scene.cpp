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

        if(node->Components.Model.has_value())
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
    std::vector<Level::NodeHandle>& outNodeHandles,
    std::vector<ShaderInterop::WorldTransform>& outWorldTransforms)
{
    // One world space transform per model instance.
    const size_t transformCount = CountWorldTransforms(level);

    outNodeHandles.clear();
    outNodeHandles.reserve(transformCount);

    // Initialize the transform buffer with the world space transform
    // of each node that contains a model instance.

    outWorldTransforms.clear();
    outWorldTransforms.reserve(transformCount);

    for(auto& handle : level.GetAllHandles())
    {
        auto node = level.GetNode(handle);
        MLG_ASSERT(node);

        if(!node->Components.Model.has_value())
        {
            continue;
        }

        outNodeHandles.push_back(handle);

        const ShaderInterop::WorldTransform transform{ .Transform = node->WorldTransform };
        outWorldTransforms.emplace_back(transform);
    }

    return WebgpuHelper::CreateStorageBuffer<WorldTransformBuffer>(outWorldTransforms, "TransformBuffer");
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

        if(node->Components.Model.has_value())
        {
            const ModelIndex modelIndex = *node->Components.Model;
            MLG_ASSERT(modelIndex.IsValid(), "Node has invalid model index");
            const ModelInstance modelInstance{ .ModelIndex{ modelIndex } };
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
BuildDrawIndirectBuffer(std::span<const ModelInstance> modelInstances, const PropKit& propKit)
{
    const std::span<const Mesh> meshes = propKit.GetMeshes();
    const std::span<const Model> models = propKit.GetModels();

    const size_t meshInstanceCount = CountMeshes(models, modelInstances);

    std::vector<ShaderInterop::DrawIndirectParams> drawIndirectParams;
    drawIndirectParams.reserve(meshInstanceCount);

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

        std::span<const Mesh> modelMeshes =
            meshes.subspan(model.FirstMesh.Value(), model.MeshCount);

        for(const auto& meshSrc : modelMeshes)
        {
            ShaderInterop::DrawIndirectParams drawParams //
                {
                    .IndexCount = meshSrc.IndexCount,
                    .InstanceCount = 1,
                    .FirstIndex = meshSrc.FirstIndex,
                    .BaseVertex = meshSrc.BaseVertex,
                    .FirstInstance = meshCount,
                };

            drawIndirectParams.emplace_back(drawParams);

            ++meshCount;
        }
    }

    return WebgpuHelper::CreateIndirectBuffer<DrawIndirectBuffer>(drawIndirectParams,
        "DrawIndirectBuffer");
}

static Result<MeshPropertiesBuffer>
BuildMeshPropertiesBuffer(std::span<const ModelInstance> modelInstances, const PropKit& propKit)
{
    const std::span<const Mesh> meshes = propKit.GetMeshes();
    const std::span<const Model> models = propKit.GetModels();

    const size_t meshInstanceCount = CountMeshes(models, modelInstances);

    uint32_t transformIndex = 0;

    std::vector<ShaderInterop::MeshProperties> meshProperties;
    meshProperties.reserve(meshInstanceCount);

    for(const auto& modelInstance : modelInstances)
    {
        MLG_ASSERT(modelInstance.ModelIndex.Value() < models.size(),
            "Model instance has invalid model index: {} (model count: {})",
            modelInstance.ModelIndex.Value(), models.size());

        const Model& model = models[modelInstance.ModelIndex.Value()];

        MLG_ASSERT(model.FirstMesh.Value() + model.MeshCount <= meshes.size(),
            "Model has invalid mesh range: first mesh {}, mesh count {} (total meshes: {})",
            model.FirstMesh.Value(), model.MeshCount, meshes.size());

        std::span<const Mesh> modelMeshes =
            meshes.subspan(model.FirstMesh.Value(), model.MeshCount);

        for(const auto& meshSrc : modelMeshes)
        {
            const BoundingSphere boundingSphere(meshSrc.BoundingBox);

            ShaderInterop::MeshProperties meshProps//
            {
                .Center = boundingSphere.GetCenter(),
                .Radius = boundingSphere.GetRadius(),
                .TransformIndex{ transformIndex },
                .MaterialIndex{ meshSrc.MaterialIndex.Value() },
            };

            meshProperties.emplace_back(meshProps);
        }

        ++transformIndex;
    }

    return WebgpuHelper::CreateStorageBuffer<MeshPropertiesBuffer>(meshProperties,
        "MeshPropertiesBuffer");
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

    std::vector<Level::NodeHandle> levelNodeHandles;
    std::vector<ShaderInterop::WorldTransform> worldTransforms;

    auto transformBuffer = BuildTransformBuffer(level, levelNodeHandles, worldTransforms);
    MLG_CHECK(transformBuffer);

    auto clipSpaceBuffer =
        WebgpuHelper::CreateStorageBuffer<ClipSpaceBuffer>(transformBuffer->Count(),
            "ClipSpaceBuffer");
    MLG_CHECK(clipSpaceBuffer);

    std::vector<ModelInstance> modelInstances;
    MLG_CHECK(CollectModelInstances(level, modelInstances));

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

    std::vector<Scene::TransformBufferIndex> transformBufferIndices;
    transformBufferIndices.reserve(levelNodeHandles.size());
    for(size_t i = 0; i < levelNodeHandles.size(); ++i)
    {
        const Scene::TransformBufferIndex index //
            {
                .NodeHandle = levelNodeHandles[i],
                .Index = i,
            };

        transformBufferIndices.push_back(index);
    }

    Scene scene(&propKit,
        *transformBuffer,
        *drawIndirectBuffer,
        *meshPropertiesBuffer,
        *cameraParamsBuf,
        *colorPipelineBindGroup0,
        *transformPipelineBindGroup0,
        std::move(modelInstances),
        std::move(worldTransforms),
        std::move(transformBufferIndices));

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
    std::vector<ShaderInterop::WorldTransform>&& worldTransforms,
    std::vector<TransformBufferIndex>&& transformBufferIndices)
    : m_PropKit(propKit),
      m_WorldTransformBuffer(worldTransformBuffer),
      m_DrawIndirectBuffer(drawIndirectBuffer),
      m_MeshPropertiesBuffer(meshPropertiesBuffer),
      m_CameraParamsBuffer(cameraParamsBuffer),
      m_ColorPipelineBindGroup0(colorPipelineBindGroup0),
      m_TransformPipelineBindGroup0(transformPipelineBindGroup0),
      m_ModelInstances(std::move(modelInstances)),
      m_WorldTransforms(std::move(worldTransforms)),
      m_TransformBufferIndices(std::move(transformBufferIndices))
{
    // Sort the transform buffer indices by node handle to allow binary search.
    std::sort(m_TransformBufferIndices.begin(), m_TransformBufferIndices.end(),
        [](const TransformBufferIndex& a, const TransformBufferIndex& b)
        {
            return a.NodeHandle < b.NodeHandle;
        });
}

Result<>
Scene::UpdateWorldTransform(const Level::NodeHandle nodeHandle, const Mat44f& worldTransform)
{
    MLG_CHECKV(nodeHandle, "Invalid node handle");

    auto index = GetTransformBufferIndex(nodeHandle);
    MLG_CHECK(index);

    m_WorldTransforms[*index] = ShaderInterop::WorldTransform //
        {
            .Transform = worldTransform,
        };

    return Result<>::Ok;
}

Result<> Scene::SyncToGpu()
{
    // Brute force copy everything for now.
    WebgpuHelper::GetDevice().GetQueue().WriteBuffer(m_WorldTransformBuffer.GetGpuBuffer(),
        0,
        reinterpret_cast<const uint8_t*>(m_WorldTransforms.data()),
        m_WorldTransforms.size() * sizeof(m_WorldTransforms[0]));

    return Result<>::Ok;
}

// private:

Result<size_t>
Scene::GetTransformBufferIndex(const Level::NodeHandle& nodeHandle) const
{
    // Binary search the buffer sorted by node handle.

    auto it = std::lower_bound(m_TransformBufferIndices.begin(),
        m_TransformBufferIndices.end(),
        nodeHandle,
        [](const TransformBufferIndex& index, const Level::NodeHandle& handle)
        {
            return index.NodeHandle < handle;
        });

    MLG_CHECKV(it != m_TransformBufferIndices.end() && it->NodeHandle == nodeHandle,
        "Transform buffer index not found for node handle");

    return it->Index;
}