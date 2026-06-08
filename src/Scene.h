#pragma once

#include "Level.h"
#include "WebgpuHelper.h"

#include <span>
#include <vector>

// Strongly-typed GPU storage buffer classes.
using WorldTransformBuffer = SemanticGpuBuffer<ShaderInterop::WorldTransform>;
using ClipSpaceBuffer = SemanticGpuBuffer<ShaderInterop::ClipSpaceTransform>;
using MeshPropertiesBuffer = SemanticGpuBuffer<ShaderInterop::MeshProperties>;
using CameraParamsBuffer = SemanticGpuBuffer<ShaderInterop::CameraParams>;

class ModelInstance
{
public:

    explicit ModelInstance(const ModelIdentifier modelId)
        : m_ModelId(modelId)
    {
        MLG_ASSERT(modelId.IsValid(), "ModelInstance cannot be created with invalid ModelIdentifier");
    }

    ModelInstance() = default;
    ~ModelInstance() = default;
    ModelInstance(const ModelInstance&) = default;
    ModelInstance& operator=(const ModelInstance&) = default;
    ModelInstance(ModelInstance&&) = default;
    ModelInstance& operator=(ModelInstance&&) = default;

    ModelIdentifier GetModelId() const { return m_ModelId; }

    void SetVisible(const bool visible) { m_IsVisible = visible; }
    bool IsVisible() const { return m_IsVisible; }

private:
    ModelIdentifier m_ModelId;

    bool m_IsVisible{ true };
};

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

    std::span<const ModelInstance> GetModelInstances() const { return m_ModelInstances; }

    DrawIndirectBuffer GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }

    CameraParamsBuffer GetCameraParamsBuffer() const { return m_CameraParamsBuffer; }

    wgpu::BindGroup GetColorPipelineBindGroup0() const { return m_ColorPipelineBindGroup0; }

    wgpu::BindGroup GetTransformPipelineBindGroup0() const { return m_TransformPipelineBindGroup0; }

    Result<> SyncFromLevel(const Level& level);

    // Sync updates from CPU -< GPU.
    Result<> SyncToGpu();

private:

    Scene(WorldTransformBuffer worldTransformBuffer,
        DrawIndirectBuffer drawIndirectBuffer,
        MeshPropertiesBuffer meshPropertiesBuffer,
        CameraParamsBuffer cameraParamsBuffer,
        wgpu::BindGroup colorPipelineBindGroup0,
        wgpu::BindGroup transformPipelineBindGroup0,
        std::vector<ModelInstance> modelInstances,
        std::vector<ShaderInterop::WorldTransform> worldTransforms,
        std::vector<Level::NodeHandle> nodeHandles);

    WorldTransformBuffer m_WorldTransformBuffer;
    DrawIndirectBuffer m_DrawIndirectBuffer;
    MeshPropertiesBuffer m_MeshPropertiesBuffer;
    CameraParamsBuffer m_CameraParamsBuffer;
    wgpu::BindGroup m_ColorPipelineBindGroup0;
    wgpu::BindGroup m_TransformPipelineBindGroup0;
    std::vector<ModelInstance> m_ModelInstances;

    // Staging buffer for world transforms.
    std::vector<ShaderInterop::WorldTransform> m_WorldTransforms;

    std::vector<Level::NodeHandle> m_NodeHandles;
};