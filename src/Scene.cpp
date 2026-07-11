#include <webgpu/webgpu_cpp.h>
#define MLG_LOGGER_NAME "SCEN"

#include "Scene.h"

#include "Camera.h"
#include "GpuHelper.h"
#include "Level.h"
#include "narrow_cast.h"
#include "PerfMetrics.h"
#include "SceneTypes.h"
#include "Timer.h"

#include <ranges>

namespace
{

size_t
CountModelInstances(const Level& level)
{
    size_t count = 0;
    for(const Level::Node& node : level.GetAllNodes())
    {
        if(node.Components.Model)
        {
            ++count;
        }
    }

    return count;
}

size_t
CountMeshInstances(const Level& level)
{
    size_t count = 0;

    for(const Level::Node& node : level.GetAllNodes())
    {
        const std::optional<const Model*> optModel = node.Components.Model;

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
    std::vector<const Level::Node*>& outNodes,
    std::vector<ShaderInterop::WorldTransform>& outWorldTransforms,
    std::vector<ModelInstance>& outModelInstances,
    std::vector<MeshInstance>& outMeshInstances)
{
    const size_t modelInstanceCount = CountModelInstances(level);
    const size_t meshInstanceCount = CountMeshInstances(level);

    outNodes.clear();
    outWorldTransforms.clear();
    outModelInstances.clear();
    outMeshInstances.clear();

    outNodes.reserve(modelInstanceCount);
    outWorldTransforms.reserve(modelInstanceCount);
    outModelInstances.reserve(modelInstanceCount);
    outMeshInstances.reserve(meshInstanceCount);

    // Initialize the transform buffer with the world space transform
    // of each node that contains a model instance.

    for(const Level::Node& node : level.GetAllNodes())
    {
        const std::optional<const Model*> optModel = node.Components.Model;

        if(!optModel)
        {
            continue;
        }

        const Model* model = *optModel;
        MLG_CHECKV(model, "Node has invalid model pointer");

        outNodes.emplace_back(&node);

        const ShaderInterop::WorldTransform worldTransform{ .Transform = node.WorldTransform };
        outWorldTransforms.emplace_back(worldTransform);

        const size_t meshInstanceOffset = outMeshInstances.size();

        for(const Mesh& mesh : model->GetMeshes())
        {
            const size_t meshIndex = outMeshInstances.size();
            outMeshInstances.emplace_back(&mesh, meshIndex);
        }

        const std::span<const MeshInstance> meshInstances =
            std::span(outMeshInstances).subspan(meshInstanceOffset, model->GetMeshes().size());

        outModelInstances.emplace_back(model, meshInstances);
    }
    
    return Result<>::Ok;
}

Result<DrawIndirectBuffer>
BuildDrawIndirectBuffer(GpuHelper& gpuHelper, std::span<const MeshInstance> meshInstances)
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

    auto buffer = gpuHelper.CreateIndirectBuffer<DrawIndirectBuffer>(drawIndirectParams.size(),
        "DrawIndirectBuffer");
    MLG_CHECK(buffer);

    buffer->Store(drawIndirectParams);

    return buffer;
}

Result<MeshPropertiesBuffer>
BuildMeshPropertiesBuffer(GpuHelper& gpuHelper,
    const std::span<const ModelInstance> modelInstances,
    const std::span<const MeshInstance> meshInstances)
{
    const size_t meshInstanceCount = meshInstances.size();

    std::vector<ShaderInterop::MeshProperties> meshProperties;
    meshProperties.reserve(meshInstanceCount);

    uint32_t transformIndex = 0;

    for(const auto& modelInstance : modelInstances)
    {
        const Model* model = modelInstance.GetModel();
        MLG_CHECK(model);
        const std::span<const Mesh> meshes = model->GetMeshes();

        for(const auto& meshSrc : meshes)
        {
            const ShaderInterop::MeshProperties meshProps//
            {
                .TransformIndex = transformIndex,
                // FIXME(KB) - reconcile material ID
                .MaterialIndex = narrow_cast<uint32_t>(meshSrc.GetMaterialId().GetValue()),
            };

            meshProperties.emplace_back(meshProps);
        }

        ++transformIndex;
    }

    auto buffer = gpuHelper.CreateStorageBuffer<MeshPropertiesBuffer>(meshProperties.size(),
        "MeshPropertiesBuffer");
    MLG_CHECK(buffer);

    buffer->Store(meshProperties);

    return buffer;
}
} // namespace

Result<Scene>
Scene::Create(GpuHelper& gpuHelper, const Level& level)
{
    Timer createTimer;
    createTimer.Start();

    std::vector<const Level::Node*> nodes;
    std::vector<ShaderInterop::WorldTransform> worldTransforms;
    std::vector<ModelInstance> modelInstances;
    std::vector<MeshInstance> meshInstances;

    MLG_CHECK(BuildScene(level, nodes, worldTransforms, modelInstances, meshInstances));

    auto transformBuffer =
        gpuHelper.CreateStorageBuffer<WorldTransformBuffer>(nodes.size(), "WorldTransforms");
    MLG_CHECK(transformBuffer);

    transformBuffer->Store(worldTransforms);

    auto clipSpaceBuffer =
        gpuHelper.CreateStorageBuffer<ClipSpaceBuffer>(nodes.size(), "ClipSpaceTransforms");
    MLG_CHECK(clipSpaceBuffer);

    auto drawIndirectBuffer = BuildDrawIndirectBuffer(gpuHelper, meshInstances);
    MLG_CHECK(drawIndirectBuffer);

    auto meshPropertiesBuffer = BuildMeshPropertiesBuffer(gpuHelper, modelInstances, meshInstances);
    MLG_CHECK(meshPropertiesBuffer);

    auto cameraParamsBuf =
        gpuHelper.CreateUniformBuffer<CameraParamsBuffer>(1, "CameraParams");
    MLG_CHECK(cameraParamsBuf);

    Scene scene(std::move(*transformBuffer),
        std::move(*clipSpaceBuffer),
        std::move(*drawIndirectBuffer),
        std::move(*meshPropertiesBuffer),
        std::move(*cameraParamsBuf),
        std::move(nodes),
        std::move(modelInstances),
        std::move(meshInstances),
        std::move(worldTransforms));

    MLG_INFO("Scene created in {} ms", createTimer.GetElapsedSeconds() * 1000);

    return std::move(scene);
}

Scene::Scene(WorldTransformBuffer&& worldTransformBuffer,
    ClipSpaceBuffer&& clipSpaceBuffer,
    DrawIndirectBuffer&& drawIndirectBuffer,
    MeshPropertiesBuffer&& meshPropertiesBuffer,
    CameraParamsBuffer&& cameraParamsBuffer,
    std::vector<const Level::Node*>&& nodes,
    std::vector<ModelInstance>&& modelInstances,
    std::vector<MeshInstance>&& meshInstances,
    std::vector<ShaderInterop::WorldTransform>&& worldTransforms)
    : m_WorldTransformBuffer(std::move(worldTransformBuffer)),
      m_ClipSpaceBuffer(std::move(clipSpaceBuffer)),
      m_DrawIndirectBuffer(std::move(drawIndirectBuffer)),
      m_MeshPropertiesBuffer(std::move(meshPropertiesBuffer)),
      m_CameraParamsBuffer(std::move(cameraParamsBuffer)),
      m_Nodes(std::move(nodes)),
      m_ModelInstances(std::move(modelInstances)),
      m_MeshInstances(std::move(meshInstances)),
      m_WorldTransforms(std::move(worldTransforms))
{
}

void
Scene::GetVisibleMeshes(const Frustum& frustum, std::vector<MeshInstance>& outVisibleMeshes) const
{
    static PerfCounter pcTotalMeshes({ .Name = "Scene.Meshes.Total" });
    static PerfCounter pcVisibleMeshes({ .Name = "Scene.Meshes.Visible" });

    pcTotalMeshes.Increment(m_MeshInstances.size());

    outVisibleMeshes.clear();

    for(const auto&& [modelInstance, worldXForm] :
        std::views::zip(m_ModelInstances, m_WorldTransforms))
    {
        if(!modelInstance.IsVisible())
        {
            continue;
        }
        
        const BoundingSphere& modelBs = worldXForm.Transform * modelInstance.GetBoundingSphere();

        const Frustum::ContainsResult result = frustum.Contains(modelBs);

        if(result == Frustum::ContainsResult::Outside)
        {
            continue;
        }

        if(result == Frustum::ContainsResult::Intersects)
        {
            // Model intersects frustum, check each mesh instance.

            for(const MeshInstance& meshInstance : modelInstance.GetMeshInstances())
            {
                const BoundingSphere& meshBs = worldXForm.Transform * meshInstance.GetBoundingSphere();

                if(Frustum::ContainsResult::Outside == frustum.Contains(meshBs))
                {
                    continue;
                }

                outVisibleMeshes.emplace_back(meshInstance);
            }
        }
        else
        {
            // Model is fully inside frustum, add all mesh instances.

            for(const MeshInstance& meshInstance : modelInstance.GetMeshInstances())
            {
                outVisibleMeshes.emplace_back(meshInstance);
            }
        }
    }

    pcVisibleMeshes.Increment(outVisibleMeshes.size());
}

Result<>
Scene::SyncFromLevel()
{
    auto view = std::views::zip(m_Nodes, m_WorldTransforms, m_ModelInstances);

    for(auto&& [node, worldXForm, modelInstance] : view)
    {
        const ShaderInterop::WorldTransform transform{ .Transform = node->WorldTransform };
        worldXForm = transform;

        modelInstance.SetVisible(node->IsVisible());
    }

    return Result<>::Ok;
}

Result<>
Scene::SyncToGpu(const wgpu::Device& gpuDevice)
{
    // Brute force copy everything for now.
    gpuDevice.GetQueue().WriteBuffer(m_WorldTransformBuffer.GetGpuBuffer(),
        0,
        m_WorldTransforms.data(),
        m_WorldTransforms.size() * sizeof(m_WorldTransforms[0]));

    return Result<>::Ok;
}
