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

static Result<TransformBuffer>
BuildTransformBuffer(std::span<const TransformDef> transformDefs, wgpu::CommandEncoder encoder)
{
    const size_t sizeofBuffer = transformDefs.size() * sizeof(ShaderTypes::MeshTransform);

    auto buffer = WebgpuHelper::CreateTypedStorageBuffer<TransformBuffer>(sizeofBuffer, "TransformBuffer");
    MLG_CHECK(buffer);

    auto mapped = buffer->Map();
    MLG_CHECK(mapped);

    ShaderTypes::MeshTransform* dst = *mapped;

    for(const TransformDef& transformDef : transformDefs)
    {
        dst->Transform = transformDef.Transform;
        ++dst;
    }

    buffer->Unmap(encoder);

    return buffer;
}

static inline size_t CountMeshes(std::span<const Model> models, std::span<const ModelInstance> modelInstances)
{
    size_t meshCount = 0;
    for(const auto& modelInstance : modelInstances)
    {
        MLG_ASSERT(modelInstance.ModelIndex < models.size(),
            "Model instance has invalid model index: {} (model count: {})",
            modelInstance.ModelIndex, models.size());

        meshCount += models[modelInstance.ModelIndex].MeshCount;
    }
    return meshCount;
}

static Result<IndirectBuffer>
BuildDrawIndirectBuffer(std::span<const Mesh> meshes,
    std::span<const Model> models,
    std::span<const ModelInstance> modelInstances,
    wgpu::CommandEncoder encoder)
{
    const size_t meshInstanceCount = CountMeshes(models, modelInstances);
    const size_t sizeofDrawIndirectBuffer =
        meshInstanceCount * sizeof(ShaderTypes::DrawIndirectParams);

    auto buffer = WebgpuHelper::CreateIndirectBuffer(sizeofDrawIndirectBuffer, "DrawIndirectBuffer");
    MLG_CHECK(buffer);

    auto mapped = buffer->Map();
    MLG_CHECK(mapped);

    ShaderTypes::DrawIndirectParams* drawParams = *mapped;

    uint32_t meshCount = 0;

    for(const auto& modelInstance : modelInstances)
    {
        MLG_ASSERT(modelInstance.ModelIndex < models.size(),
            "Model instance has invalid model index: {} (model count: {})",
            modelInstance.ModelIndex, models.size());

        const Model& model = models[modelInstance.ModelIndex];

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
    const size_t sizeofBuffer = meshInstanceCount * sizeof(ShaderTypes::MeshProperties);

    auto buffer = WebgpuHelper::CreateTypedStorageBuffer<MeshPropertiesBuffer>(sizeofBuffer, "MeshPropertiesBuffer");
    MLG_CHECK(buffer);

    auto mapped = buffer->Map();
    MLG_CHECK(mapped);

    ShaderTypes::MeshProperties* meshPropertiesDst = *mapped;

    uint32_t meshCount = 0;

    for(const auto& modelInstance : modelInstances)
    {
        MLG_ASSERT(modelInstance.ModelIndex < models.size(),
            "Model instance has invalid model index: {} (model count: {})",
            modelInstance.ModelIndex, models.size());

        const Model& model = models[modelInstance.ModelIndex];

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
                    .TransformIndex = modelInstance.TransformIndex,
                    .MaterialIndex = meshSrc->MaterialIndex,
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

Result<>
Scene::Create(const SceneDef& sceneDef, const PropKit& propKit, Scene& outScene)
{
    Stopwatch createTimer;
    createTimer.Mark();

    wgpu::CommandEncoder encoder = WebgpuHelper::GetDevice().CreateCommandEncoder();

    const std::span<const Mesh> meshes = propKit.GetMeshes();
    const std::span<const Model> models = propKit.GetModels();

    auto transformBuffer = BuildTransformBuffer(sceneDef.TransformDefs, encoder);
    MLG_CHECK(transformBuffer);

    auto drawIndirectBuffer = BuildDrawIndirectBuffer(meshes, models, sceneDef.ModelInstances, encoder);
    MLG_CHECK(drawIndirectBuffer);

    auto meshPropertiesBuffer = BuildMeshPropertiesBuffer(meshes, models, sceneDef.ModelInstances, encoder);
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

    std::vector<ModelInstance> modelInstances(sceneDef.ModelInstances.begin(),
        sceneDef.ModelInstances.end());

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