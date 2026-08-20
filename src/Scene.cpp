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
CountMeshInstances(const std::span<const ModelNode> modelNodes)
{
    size_t count = 0;

    for(const ModelNode& node : modelNodes)
    {
        count += node.GetModel()->GetMeshes().size();
    }

    return count;
}

Result<>
BuildScene(const Level& level,
    std::vector<ModelInstance>& outModelInstances,
    std::vector<MeshInstance>& outMeshInstances)
{
    const std::span modelNodes = level.GetAllModelNodes();
    const size_t modelInstanceCount = modelNodes.size();
    const size_t meshInstanceCount = CountMeshInstances(modelNodes);

    outModelInstances.clear();
    outMeshInstances.clear();

    outModelInstances.reserve(modelInstanceCount);
    outMeshInstances.reserve(meshInstanceCount);

    // Initialize the transform buffer with the world space transform
    // of each node that contains a model instance.

    for(const ModelNode& node : modelNodes)
    {
        const Model* model = node.GetModel();

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

Result<GpuDrawIndirectBuffer>
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

        drawIndirectParams.push_back(drawParams);
    }

    auto buffer = gpuHelper.CreateIndirectBuffer<GpuDrawIndirectBuffer>(drawIndirectParams.size(),
        "DrawIndirectBuffer");
    MLG_CHECK(buffer);

    buffer->Store(drawIndirectParams);

    return buffer;
}

Result<GpuMeshPropertiesBuffer>
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
            const ShaderInterop::MeshProperties meshProps //
                {
                    .TransformIndex = transformIndex,
                    // FIXME(KB) - reconcile material ID
                    .MaterialIndex = narrow_cast<uint32_t>(meshSrc.GetMaterialId().GetValue()),
                };

            meshProperties.push_back(meshProps);
        }

        ++transformIndex;
    }

    auto buffer = gpuHelper.CreateStorageBuffer<GpuMeshPropertiesBuffer>(meshProperties.size(),
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

    std::vector<ModelInstance> modelInstances;
    std::vector<MeshInstance> meshInstances;
    std::vector<const ModelNode*> modelNodes;

    modelNodes.reserve(level.GetAllModelNodes().size());
    for(const ModelNode& node : level.GetAllModelNodes())
    {
        modelNodes.push_back(&node);
    }

    MLG_CHECK(BuildScene(level, modelInstances, meshInstances));

    auto transformBuffer = gpuHelper.CreateStorageBuffer<GpuWorldTransformBuffer>(modelNodes.size(),
        "WorldTransforms");
    MLG_CHECK(transformBuffer);

    auto clipSpaceBuffer =
        gpuHelper.CreateStorageBuffer<GpuClipSpaceBuffer>(modelNodes.size(), "ClipSpaceTransforms");
    MLG_CHECK(clipSpaceBuffer);

    auto drawIndirectBuffer = BuildDrawIndirectBuffer(gpuHelper, meshInstances);
    MLG_CHECK(drawIndirectBuffer);

    auto meshPropertiesBuffer = BuildMeshPropertiesBuffer(gpuHelper, modelInstances, meshInstances);
    MLG_CHECK(meshPropertiesBuffer);

    auto cameraParamsBuf = gpuHelper.CreateUniformBuffer<GpuCameraParamsBuffer>(1, "CameraParams");
    MLG_CHECK(cameraParamsBuf);

    Scene scene(gpuHelper.GetDevice(),
        std::move(*transformBuffer),
        std::move(*clipSpaceBuffer),
        std::move(*drawIndirectBuffer),
        std::move(*meshPropertiesBuffer),
        std::move(*cameraParamsBuf),
        std::move(modelInstances),
        std::move(meshInstances),
        std::move(modelNodes));

    MLG_CHECK(scene.SyncToGpu());

    MLG_INFO("Scene created in {} ms", createTimer.GetElapsedSeconds() * 1000);

    return std::move(scene);
}

Scene::Scene(const wgpu::Device& gpuDevice,
    GpuWorldTransformBuffer&& worldTransformBuffer,
    GpuClipSpaceBuffer&& clipSpaceBuffer,
    GpuDrawIndirectBuffer&& drawIndirectBuffer,
    GpuMeshPropertiesBuffer&& meshPropertiesBuffer,
    GpuCameraParamsBuffer&& cameraParamsBuffer,
    std::vector<ModelInstance>&& modelInstances,
    std::vector<MeshInstance>&& meshInstances,
    std::vector<const ModelNode*>&& modelNodes)
    : m_GpuDevice(&gpuDevice),
      m_WorldTransformBuffer(std::move(worldTransformBuffer)),
      m_ClipSpaceBuffer(std::move(clipSpaceBuffer)),
      m_DrawIndirectBuffer(std::move(drawIndirectBuffer)),
      m_MeshPropertiesBuffer(std::move(meshPropertiesBuffer)),
      m_CameraParamsBuffer(std::move(cameraParamsBuffer)),
      m_ModelInstances(std::move(modelInstances)),
      m_MeshInstances(std::move(meshInstances)),
      m_ModelNodes(std::move(modelNodes))
{
}

void
Scene::GetVisibleMeshes(const Frustum& frustum, std::vector<MeshInstance>& outVisibleMeshes) const
{
    static PerfCounter pcTotalMeshes({ .Name = "Scene.Meshes.Total" });
    static PerfCounter pcVisibleMeshes({ .Name = "Scene.Meshes.Visible" });

    pcTotalMeshes.Increment(m_MeshInstances.size());

    outVisibleMeshes.clear();

    for(const auto&& [modelInstance, modelNode] : std::views::zip(m_ModelInstances, m_ModelNodes))
    {
        if(!modelInstance.IsVisible())
        {
            continue;
        }

        const BoundingSphere& modelBs =
            modelNode->GetWorldTransform() * modelInstance.GetBoundingSphere();

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
                const BoundingSphere& meshBs =
                    modelNode->GetWorldTransform() * meshInstance.GetBoundingSphere();

                if(Frustum::ContainsResult::Outside == frustum.Contains(meshBs))
                {
                    continue;
                }

                outVisibleMeshes.push_back(meshInstance);
            }
        }
        else
        {
            // Model is fully inside frustum, add all mesh instances.

            for(const MeshInstance& meshInstance : modelInstance.GetMeshInstances())
            {
                outVisibleMeshes.push_back(meshInstance);
            }
        }
    }

    pcVisibleMeshes.Increment(outVisibleMeshes.size());
}

Result<>
Scene::SyncToGpu()
{
    // Brute force copy everything for now.
    uint64_t bufferOffset = 0;
    for(size_t i = 0; i < m_ModelNodes.size(); ++i)
    {
        const ModelNode& node = *m_ModelNodes[i];

        const ShaderInterop::WorldTransform transform{ .Transform = node.GetWorldTransform() };
        m_GpuDevice->GetQueue().WriteBuffer(m_WorldTransformBuffer.GetGpuBuffer(),
            bufferOffset,
            &transform,
            sizeof(transform));

        bufferOffset += sizeof(transform);

        m_ModelInstances[i].SetVisible(node.IsVisible());
    }

    return Result<>::Ok;
}
