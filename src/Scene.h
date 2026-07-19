#pragma once

#include "GpuTypes.h"
#include "Level.h"
#include "SceneTypes.h"

#include <span>
#include <vector>

class Frustum;
class GpuHelper;

class Scene
{
public:
    static Result<Scene> Create(GpuHelper& gpuHelper, const Level& level);

    Scene() = delete;
    ~Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&& other) = default;
    Scene& operator=(Scene&& other) = default;

    std::span<const Level::Node* const> GetNodes() const { return m_Nodes; }
    
    std::span<const ModelInstance> GetModelInstances() const { return m_ModelInstances; }

    std::span<const ShaderInterop::WorldTransform> GetWorldTransforms() const
    {
        return m_WorldTransforms;
    }

    void GetVisibleMeshes(const Frustum& frustum,
        std::vector<MeshInstance>& outVisibleMeshes) const;

    GpuWorldTransformBuffer GetWorldTransformBuffer() const { return m_WorldTransformBuffer; }

    GpuClipSpaceBuffer GetClipSpaceBuffer() const { return m_ClipSpaceBuffer; }

    GpuDrawIndirectBuffer GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }

    GpuMeshPropertiesBuffer GetMeshPropertiesBuffer() const { return m_MeshPropertiesBuffer; }

    GpuCameraParamsBuffer GetCameraParamsBuffer() const { return m_CameraParamsBuffer; }

    Result<> SyncFromLevel();

    // Sync updates from CPU -> GPU.
    Result<> SyncToGpu(const wgpu::Device& gpuDevice);

private:

    Scene(GpuWorldTransformBuffer&& worldTransformBuffer,
        GpuClipSpaceBuffer&& clipSpaceBuffer,
        GpuDrawIndirectBuffer&& drawIndirectBuffer,
        GpuMeshPropertiesBuffer&& meshPropertiesBuffer,
        GpuCameraParamsBuffer&& cameraParamsBuffer,
        std::vector<const Level::Node*>&& nodes,
        std::vector<ModelInstance>&& modelInstances,
        std::vector<MeshInstance>&& meshInstances,
        std::vector<ShaderInterop::WorldTransform>&& worldTransforms);

    GpuWorldTransformBuffer m_WorldTransformBuffer;
    GpuClipSpaceBuffer m_ClipSpaceBuffer;
    GpuDrawIndirectBuffer m_DrawIndirectBuffer;
    GpuMeshPropertiesBuffer m_MeshPropertiesBuffer;
    GpuCameraParamsBuffer m_CameraParamsBuffer;

    std::vector<const Level::Node*> m_Nodes;
    std::vector<ModelInstance> m_ModelInstances;
    std::vector<MeshInstance> m_MeshInstances;

    // Staging buffer for world transforms.
    std::vector<ShaderInterop::WorldTransform> m_WorldTransforms;
};