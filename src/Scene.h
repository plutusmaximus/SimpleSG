#pragma once

#include "Level.h"
#include "SceneTypes.h"
#include "shaders/GpuBufferTypes.h"

#include <span>
#include <vector>

class Frustum;

class Scene
{
public:
    static Result<Scene> Create(const Level& level, const PropKit& propKit);

    Scene() = delete;
    ~Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&& other) = default;
    Scene& operator=(Scene&& other) = default;

    std::span<const Level::NodeHandle> GetNodeHandles() const { return m_NodeHandles; }
    
    std::span<const ModelInstance> GetModelInstances() const { return m_ModelInstances; }

    std::span<const ShaderInterop::WorldTransform> GetWorldTransforms() const
    {
        return m_WorldTransforms;
    }

    void GetVisibleMeshes(const Frustum& frustum,
        const PropKit& propKit,
        std::vector<MeshInstance>& outVisibleMeshes) const;

    DrawIndirectBuffer GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }

    CameraParamsBuffer GetCameraParamsBuffer() const { return m_CameraParamsBuffer; }

    wgpu::BindGroup GetColorShaderBindGroup() const { return m_ColorShaderBindGroup; }

    wgpu::BindGroup GetTransformShaderBindGroup() const { return m_TransformShaderBindGroup; }

    Result<> SyncFromLevel(const Level& level);

    // Sync updates from CPU -< GPU.
    Result<> SyncToGpu();

private:

    Scene(WorldTransformBuffer worldTransformBuffer,
        DrawIndirectBuffer drawIndirectBuffer,
        MeshPropertiesBuffer meshPropertiesBuffer,
        CameraParamsBuffer cameraParamsBuffer,
        wgpu::BindGroup colorShaderBindGroup,
        wgpu::BindGroup transformShaderBindGroup,
        std::vector<Level::NodeHandle> nodeHandles,
        std::vector<ModelInstance> modelInstances,
        std::vector<ShaderInterop::WorldTransform> worldTransforms);

    WorldTransformBuffer m_WorldTransformBuffer;
    DrawIndirectBuffer m_DrawIndirectBuffer;
    MeshPropertiesBuffer m_MeshPropertiesBuffer;
    CameraParamsBuffer m_CameraParamsBuffer;
    wgpu::BindGroup m_ColorShaderBindGroup;
    wgpu::BindGroup m_TransformShaderBindGroup;

    std::vector<Level::NodeHandle> m_NodeHandles;
    std::vector<ModelInstance> m_ModelInstances;

    // Staging buffer for world transforms.
    std::vector<ShaderInterop::WorldTransform> m_WorldTransforms;
};