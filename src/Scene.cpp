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

static Result<>
Validate(const LevelDef& levelDef, const PropKit& propKit)
{
    for(const auto& nodeDef : levelDef.NodeDefs)
    {
        auto assembly = propKit.GetAssembly(nodeDef.AssemblyName);
        MLG_CHECKV(assembly);
    }

    return Result<>::Ok;
}

static size_t
CountModelInstances(const AssemblyNode& assemblyNode)
{
    size_t count = assemblyNode.ModelIndex.IsValid()
                       ? 1
                       : 0; // Count the current node if it has a valid model.

    for(const auto& childNode : assemblyNode.Children)
    {
        count += CountModelInstances(childNode);
    }

    return count;
}

static Result<size_t>
CountModelInstances(const LevelDef& levelDef, const PropKit& propKit)
{
    size_t count = 0;

    for(const auto& nodeDef : levelDef.NodeDefs)
    {
        auto assembly = propKit.GetAssembly(nodeDef.AssemblyName);
        MLG_CHECKV(assembly);
        auto rootNode = propKit.GetAssemblyNode(assembly->RootNodeIndex);
        MLG_CHECKV(rootNode);
        count += CountModelInstances(**rootNode);
    }

    return count;
}

static Result<size_t>
CollectTransforms(const AssemblyNode& node,
    const Mat44f& parentTransform,
    MappedGpuBuffer<ShaderInterop::WorldTransform>& transforms,
    const size_t index)
{
    size_t currentIndex = index;
    ShaderInterop::WorldTransform curTransform{ .Transform = parentTransform * node.Transform.ToMatrix() };

    if(node.ModelIndex.IsValid())
    {
        transforms.Store(currentIndex, curTransform);

        ++currentIndex;
    }

    for(const auto& childNode : node.Children)
    {
        auto result = CollectTransforms(childNode, curTransform.Transform, transforms, currentIndex);
        MLG_CHECK(result);
        currentIndex += *result;
    }
    return (currentIndex - index);
}

static Result<WorldTransformBuffer>
BuildTransformBuffer(const LevelDef& levelDef, const PropKit& propKit, wgpu::CommandEncoder encoder)
{
    auto modelCount = CountModelInstances(levelDef, propKit);
    MLG_CHECK(modelCount);

    const size_t sizeofBuffer = *modelCount * sizeof(ShaderInterop::WorldTransform);

    auto buffer =
        WebgpuHelper::CreateSemanticStorageBuffer<WorldTransformBuffer>(sizeofBuffer, "TransformBuffer");
    MLG_CHECK(buffer);

    auto mapped = buffer->Map();
    MLG_CHECK(mapped);

    size_t count = 0;
    for(const auto& nodeDef : levelDef.NodeDefs)
    {
        auto assembly = propKit.GetAssembly(nodeDef.AssemblyName);
        MLG_CHECK(assembly);

        auto rootNode = propKit.GetAssemblyNode(assembly->RootNodeIndex);
        MLG_CHECK(rootNode);

        auto result = CollectTransforms(**rootNode, nodeDef.Transform.ToMatrix(), *mapped, count);
        MLG_CHECK(result);
        count += *result;
    }

    buffer->Unmap(encoder);

    return buffer;
}

static Result<>
CollectModelInstances(const AssemblyNode& node, std::vector<ModelInstance>& outModelInstances)
{
    if(node.ModelIndex.IsValid())
    {
        const ModelInstance modelInstance{ .ModelIndex{ node.ModelIndex } };
        outModelInstances.push_back(modelInstance);
    }

    for(const auto& childNode : node.Children)
    {
        MLG_CHECK(CollectModelInstances(childNode, outModelInstances));
    }

    return Result<>::Ok;
}

static Result<>
CollectModelInstances(
    const LevelDef& levelDef, std::vector<ModelInstance>& outModelInstances, const PropKit& propKit)
{
    auto modelInstanceCount = CountModelInstances(levelDef, propKit);
    MLG_CHECK(modelInstanceCount);

    outModelInstances.clear();
    outModelInstances.reserve(*modelInstanceCount);

    for(const auto& nodeDef : levelDef.NodeDefs)
    {
        auto assembly = propKit.GetAssembly(nodeDef.AssemblyName);
        MLG_CHECKV(assembly);
        auto rootNode = propKit.GetAssemblyNode(assembly->RootNodeIndex);
        MLG_CHECKV(rootNode);
        MLG_CHECK(CollectModelInstances(**rootNode, outModelInstances));
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

    auto mapped = buffer->Map();
    MLG_CHECK(mapped);

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

            mapped->Store(meshCount, drawParams);

            ++meshCount;
            ++meshSrc;
        }
    }

    buffer->Unmap(encoder);

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

    auto mapped = buffer->Map();
    MLG_CHECK(mapped);

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

            mapped->Store(meshCount, meshProps);

            ++meshCount;
            ++meshSrc;
        }

        ++transformIndex;
    }

    buffer->Unmap(encoder);

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
Scene::Create(const LevelDef& levelDef, const PropKit& propKit, Scene& outScene)
{
    Stopwatch createTimer;
    createTimer.Mark();

    MLG_CHECK(Validate(levelDef, propKit));

    wgpu::CommandEncoder encoder = WebgpuHelper::GetDevice().CreateCommandEncoder();

    auto transformBuffer = BuildTransformBuffer(levelDef, propKit, encoder);
    MLG_CHECK(transformBuffer);

    auto clipSpaceBuffer =
        WebgpuHelper::CreateSemanticStorageBuffer<ClipSpaceBuffer>(transformBuffer->GetSize(),
            "ClipSpaceBuffer");
    MLG_CHECK(clipSpaceBuffer);

    std::vector<ModelInstance> modelInstances;
    MLG_CHECK(CollectModelInstances(levelDef, modelInstances, propKit));

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

    Scene scene(&propKit,
        *transformBuffer,
        *drawIndirectBuffer,
        *meshPropertiesBuffer,
        *cameraParamsBuf,
        *colorPipelineBindGroup0,
        *transformPipelineBindGroup0,
        std::move(modelInstances));

    outScene = std::move(scene);

    MLG_INFO("Scene created in {} ms", createTimer.ElapsedSeconds() * 1000);

    return Result<>::Ok;
}