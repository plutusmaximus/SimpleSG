#pragma once

#include "Level.h"
#include "SceneTypes.h"
#include "shaders/GpuBufferTypes.h"

#include <span>
#include <vector>

class Frustum;
class GpuHelper;

class Scene
{
public:
    static Result<Scene> Create(GpuHelper& gpuHelper, const Level& level, const PropKit& propKit);

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

    WorldTransformBuffer GetWorldTransformBuffer() const { return m_WorldTransformBuffer; }

    ClipSpaceBuffer GetClipSpaceBuffer() const { return m_ClipSpaceBuffer; }

    DrawIndirectBuffer GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }

    MeshPropertiesBuffer GetMeshPropertiesBuffer() const { return m_MeshPropertiesBuffer; }

    CameraParamsBuffer GetCameraParamsBuffer() const { return m_CameraParamsBuffer; }

    wgpu::BindGroup GetColorShaderBindGroup() const { return m_ColorShaderBindGroup; }

    wgpu::BindGroup GetTransformShaderBindGroup() const { return m_TransformShaderBindGroup; }

    Result<> SyncFromLevel();

    // Sync updates from CPU -> GPU.
    Result<> SyncToGpu(const wgpu::Device& gpuDevice);

private:

    Scene(WorldTransformBuffer&& worldTransformBuffer,
        ClipSpaceBuffer&& clipSpaceBuffer,
        DrawIndirectBuffer&& drawIndirectBuffer,
        MeshPropertiesBuffer&& meshPropertiesBuffer,
        CameraParamsBuffer&& cameraParamsBuffer,
        wgpu::BindGroup&& colorShaderBindGroup,
        wgpu::BindGroup&& transformShaderBindGroup,
        std::vector<const Level::Node*>&& nodes,
        std::vector<ModelInstance>&& modelInstances,
        std::vector<MeshInstance>&& meshInstances,
        std::vector<ShaderInterop::WorldTransform>&& worldTransforms);

    WorldTransformBuffer m_WorldTransformBuffer;
    ClipSpaceBuffer m_ClipSpaceBuffer;
    DrawIndirectBuffer m_DrawIndirectBuffer;
    MeshPropertiesBuffer m_MeshPropertiesBuffer;
    CameraParamsBuffer m_CameraParamsBuffer;
    wgpu::BindGroup m_ColorShaderBindGroup;
    wgpu::BindGroup m_TransformShaderBindGroup;

    std::vector<const Level::Node*> m_Nodes;
    std::vector<ModelInstance> m_ModelInstances;
    std::vector<MeshInstance> m_MeshInstances;

    // Staging buffer for world transforms.
    std::vector<ShaderInterop::WorldTransform> m_WorldTransforms;
};