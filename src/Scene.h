#pragma once

#include "GpuTypes.h"
#include "Level.h"
#include "SceneTypes.h"

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

    void GetVisibleMeshes(const Frustum& frustum,
        std::vector<MeshInstance>& outVisibleMeshes) const;

    GpuWorldTransformBuffer GetWorldTransformBuffer() const { return m_WorldTransformBuffer; }

    GpuClipSpaceBuffer GetClipSpaceBuffer() const { return m_ClipSpaceBuffer; }

    GpuDrawIndirectBuffer GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }

    GpuMeshPropertiesBuffer GetMeshPropertiesBuffer() const { return m_MeshPropertiesBuffer; }

    GpuCameraParamsBuffer GetCameraParamsBuffer() const { return m_CameraParamsBuffer; }

    // Sync updates from CPU -> GPU.
    Result<> SyncToGpu();

private:
    Scene(const wgpu::Device& gpuDevice,
        GpuWorldTransformBuffer&& worldTransformBuffer,
        GpuClipSpaceBuffer&& clipSpaceBuffer,
        GpuDrawIndirectBuffer&& drawIndirectBuffer,
        GpuMeshPropertiesBuffer&& meshPropertiesBuffer,
        GpuCameraParamsBuffer&& cameraParamsBuffer,
        std::vector<ModelInstance>&& modelInstances,
        std::vector<MeshInstance>&& meshInstances,
        std::vector<const ModelNode*>&& modelNodes);

    const wgpu::Device* m_GpuDevice{ nullptr };

    GpuWorldTransformBuffer m_WorldTransformBuffer;
    GpuClipSpaceBuffer m_ClipSpaceBuffer;
    GpuDrawIndirectBuffer m_DrawIndirectBuffer;
    GpuMeshPropertiesBuffer m_MeshPropertiesBuffer;
    GpuCameraParamsBuffer m_CameraParamsBuffer;

    std::vector<ModelInstance> m_ModelInstances;
    std::vector<MeshInstance> m_MeshInstances;

    std::vector<const ModelNode*> m_ModelNodes;
};