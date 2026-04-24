#define __LOGGER_NAME__ "SCEN"

#include "Scene.h"

#include "PropKit.h"
#include "Stopwatch.h"

namespace
{
struct ColorPipelineResources
{
    TransformBuffer TransformBuffer;
    MaterialConstantsBuffer MaterialConstantsBuffer;
    MeshPropertiesBuffer MeshPropertiesBuffer;
};

struct TransformPipelineResources
{
    TransformBuffer TransformBuffer;
};
}

static Result<> ValidateNode(const SceneNodeDef& nodeDef, const PropKit& propKit)
{
    if(nodeDef.ModelIndex.IsValid())
    {
        const std::span<const Model> models = propKit.GetModels();

        MLG_CHECKV(nodeDef.ModelIndex.Value() < models.size(),
            "Node definition '{}' has invalid model index {}, model count {}",
            nodeDef.Name, nodeDef.ModelIndex.Value(), models.size());
    }

    for(const auto& childNodeDef : nodeDef.Children)
    {
        MLG_CHECK(ValidateNode(childNodeDef, propKit));
    }

    return Result<>::Ok;
}

static Result<> Validate(const SceneDef& sceneDef, const PropKit& propKit)
{
    for(const auto& nodeDef : sceneDef.NodeDefs)
    {
        MLG_CHECK(ValidateNode(nodeDef, propKit));
    }

    return Result<>::Ok;
}

static Result<>
CollectTransforms(const SceneNodeDef& nodeDef, std::vector<Mat44f>& transforms)
{
    transforms.push_back(nodeDef.Transform);
    for(const auto& childNodeDef : nodeDef.Children)
    {
        MLG_CHECK(CollectTransforms(childNodeDef, transforms));
    }
    return Result<>::Ok;
};

static Result<TransformBuffer>
BuildTransformBuffer(std::span<const SceneNodeDef> nodeDefs, wgpu::CommandEncoder encoder)
{
    std::vector<Mat44f> transforms;
    for(const auto& nodeDef : nodeDefs)
    {
        MLG_CHECK(CollectTransforms(nodeDef, transforms));
    }

    const size_t sizeofBuffer = transforms.size() * sizeof(ShaderInterop::MeshTransform);

    auto buffer =
        WebgpuHelper::CreateSemanticStorageBuffer<TransformBuffer>(sizeofBuffer, "TransformBuffer");
    MLG_CHECK(buffer);

    auto mapped = buffer->Map();
    MLG_CHECK(mapped);

    std::memcpy(*mapped, transforms.data(), sizeofBuffer);

    buffer->Unmap(encoder);

    return buffer;
}

static inline size_t CountMeshes(std::span<const Model> models, std::span<const ModelInstance> modelInstances)
{
    size_t meshCount = 0;
    for(const auto& modelInstance : modelInstances)
    {
        MLG_ASSERT(modelInstance.ModelIndex.Value() < models.size(),
            "Model instance has invalid model index: {} (model count: {})",
            modelInstance.ModelIndex.Value(), models.size());

        meshCount += models[modelInstance.ModelIndex.Value()].MeshCount;
    }
    return meshCount;
}

static Result<DrawIndirectBuffer>
BuildDrawIndirectBuffer(std::span<const Mesh> meshes,
    std::span<const Model> models,
    std::span<const ModelInstance> modelInstances,
    wgpu::CommandEncoder encoder)
{
    const size_t meshInstanceCount = CountMeshes(models, modelInstances);
    const size_t sizeofDrawIndirectBuffer =
        meshInstanceCount * sizeof(ShaderInterop::DrawIndirectParams);

    auto buffer = WebgpuHelper::CreateSemanticIndirectBuffer<DrawIndirectBuffer>(
        sizeofDrawIndirectBuffer,
        "DrawIndirectBuffer");
    MLG_CHECK(buffer);

    auto mapped = buffer->Map();
    MLG_CHECK(mapped);

    ShaderInterop::DrawIndirectParams* drawParams = *mapped;

    uint32_t meshCount = 0;

    for(const auto& modelInstance : modelInstances)
    {
        MLG_ASSERT(modelInstance.ModelIndex.Value() < models.size(),
            "Model instance has invalid model index: {} (model count: {})",
            modelInstance.ModelIndex.Value(), models.size());

        const Model& model = models[modelInstance.ModelIndex.Value()];

        MLG_ASSERT(model.FirstMesh + model.MeshCount <= meshes.size(),
            "Model has invalid mesh range: first mesh {}, mesh count {} (total meshes: {})",
            model.FirstMesh, model.MeshCount, meshes.size());

        const Mesh* meshSrc = &meshes[model.FirstMesh];

        for(uint32_t i = 0; i < model.MeshCount; ++i)
        {
            drawParams[meshCount] = //
            {
                .IndexCount = meshSrc->IndexCount,
                .InstanceCount = 1,
                .FirstIndex = meshSrc->FirstIndex,
                .BaseVertex = meshSrc->BaseVertex,
                .FirstInstance = meshCount,
            };

            ++meshCount;
            ++meshSrc;
        }
    }

    buffer->Unmap(encoder);

    return buffer;
}

static Result<MeshPropertiesBuffer>
BuildMeshPropertiesBuffer(std::span<const Mesh> meshes,
    std::span<const Model> models,
    std::span<const ModelInstance> modelInstances,
    wgpu::CommandEncoder encoder)
{
    const size_t meshInstanceCount = CountMeshes(models, modelInstances);
    const size_t sizeofBuffer = meshInstanceCount * sizeof(ShaderInterop::MeshProperties);

    auto buffer = WebgpuHelper::CreateSemanticStorageBuffer<MeshPropertiesBuffer>(sizeofBuffer, "MeshPropertiesBuffer");
    MLG_CHECK(buffer);

    auto mapped = buffer->Map();
    MLG_CHECK(mapped);

    ShaderInterop::MeshProperties* meshPropertiesDst = *mapped;

    uint32_t meshCount = 0;

    for(const auto& modelInstance : modelInstances)
    {
        MLG_ASSERT(modelInstance.ModelIndex.Value() < models.size(),
            "Model instance has invalid model index: {} (model count: {})",
            modelInstance.ModelIndex.Value(), models.size());

        const Model& model = models[modelInstance.ModelIndex.Value()];

        MLG_ASSERT(model.FirstMesh + model.MeshCount <= meshes.size(),
            "Model has invalid mesh range: first mesh {}, mesh count {} (total meshes: {})",
            model.FirstMesh, model.MeshCount, meshes.size());

        const Mesh* meshSrc = &meshes[model.FirstMesh];

        for(uint32_t i = 0; i < model.MeshCount; ++i)
        {
            const BoundingSphere boundingSphere(meshSrc->BoundingBox);

            meshPropertiesDst[meshCount] = //
                {
                    .Center = boundingSphere.GetCenter(),
                    .Radius = boundingSphere.GetRadius(),
                    .NodeIndex = modelInstance.NodeIndex.Value(),
                    .MaterialIndex = meshSrc->MaterialIndex.Value(),
                };

            ++meshCount;
            ++meshSrc;
        }
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
            .buffer = colorPipelineResources.TransformBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorPipelineResources.TransformBuffer.GetSize(),
        },
        {
            .binding = 1,
            .buffer = colorPipelineResources.MeshPropertiesBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorPipelineResources.MeshPropertiesBuffer.GetSize(),
        },
        {
            .binding = 2,
            .buffer = colorPipelineResources.MaterialConstantsBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = colorPipelineResources.MaterialConstantsBuffer.GetSize(),
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
            .buffer = transformPipelineResources.TransformBuffer.GetGpuBuffer(),
            .offset = 0,
            .size = transformPipelineResources.TransformBuffer.GetSize(),
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

static Result<>
CollectModelInstances(const SceneNodeDef& nodeDef,
    NodeIndex& nodeIndex,
    std::vector<ModelInstance>& outModelInstances)
{
    if(nodeDef.ModelIndex.IsValid())
    {
        const ModelInstance modelInstance{ nodeDef.ModelIndex, nodeIndex };
        outModelInstances.push_back(modelInstance);
    }

    nodeIndex = NodeIndex(nodeIndex.Value() + 1);

    for(const auto& childNodeDef : nodeDef.Children)
    {
        MLG_CHECK(CollectModelInstances(childNodeDef, nodeIndex, outModelInstances));
    }

    return Result<>::Ok;
}

Result<>
Scene::Create(const SceneDef& sceneDef, const PropKit& propKit, Scene& outScene)
{
    Stopwatch createTimer;
    createTimer.Mark();

    MLG_CHECK(Validate(sceneDef, propKit));

    wgpu::CommandEncoder encoder = WebgpuHelper::GetDevice().CreateCommandEncoder();

    const std::span<const Mesh> meshes = propKit.GetMeshes();
    const std::span<const Model> models = propKit.GetModels();

    auto transformBuffer = BuildTransformBuffer(sceneDef.NodeDefs, encoder);
    MLG_CHECK(transformBuffer);

    std::vector<ModelInstance> modelInstances;
    NodeIndex nodeIndex{ 0 };
    for(const auto& nodeDef : sceneDef.NodeDefs)
    {
        MLG_CHECK(CollectModelInstances(nodeDef, nodeIndex, modelInstances));
    }

    auto drawIndirectBuffer = BuildDrawIndirectBuffer(meshes, models, modelInstances, encoder);
    MLG_CHECK(drawIndirectBuffer);

    auto meshPropertiesBuffer = BuildMeshPropertiesBuffer(meshes, models, modelInstances, encoder);
    MLG_CHECK(meshPropertiesBuffer);

    ColorPipelineResources colorPipelineResources //
    {
        .TransformBuffer = *transformBuffer,
        .MaterialConstantsBuffer = propKit.GetMaterialConstantsBuffer(),
        .MeshPropertiesBuffer = *meshPropertiesBuffer,
    };

    auto colorPipelineBindGroup0 = CreateColorPipelineBindGroup0(colorPipelineResources);
    MLG_CHECK(colorPipelineBindGroup0);

    TransformPipelineResources transformPipelineResources //
    {
        .TransformBuffer = *transformBuffer,
    };

    auto transformPipelineBindGroup0 = CreateTransformPipelineBindGroup0(transformPipelineResources);
    MLG_CHECK(transformPipelineBindGroup0);

    wgpu::CommandBuffer commandBuffer = encoder.Finish();
    WebgpuHelper::GetDevice().GetQueue().Submit(1, &commandBuffer);

    Scene scene(&propKit,
        *transformBuffer,
        *drawIndirectBuffer,
        *meshPropertiesBuffer,
        *colorPipelineBindGroup0,
        *transformPipelineBindGroup0,
        std::move(modelInstances));

    outScene = std::move(scene);

    MLG_INFO("Scene created in {} ms", createTimer.ElapsedSeconds() * 1000);

    return Result<>::Ok;
}