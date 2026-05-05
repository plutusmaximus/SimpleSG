#pragma once

#include "Level.h"
#include "PropKit.h"
#include "WebgpuHelper.h"

#include <span>
#include <vector>

// Strongly-typed GPU storage buffer classes.
using WorldTransformBuffer = SemanticGpuBuffer<ShaderInterop::WorldTransform>;
using ClipSpaceBuffer = SemanticGpuBuffer<ShaderInterop::ClipSpaceTransform>;
using MeshPropertiesBuffer = SemanticGpuBuffer<ShaderInterop::MeshProperties>;
using CameraParamsBuffer = SemanticGpuBuffer<ShaderInterop::CameraParams>;

struct ModelInstance
{
    ModelIndex ModelIndex{ ModelIndex::INVALID };
};

class Scene
{
public:
    static Result<> Create(const Level& level, const PropKit& propKit, Scene& outScene);

    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&& other) = default;
    Scene& operator=(Scene&& other) = default;

    const std::span<const ModelInstance> GetModelInstances() const { return m_ModelInstances; }

    DrawIndirectBuffer GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }

    CameraParamsBuffer GetCameraParamsBuffer() const { return m_CameraParamsBuffer; }

    wgpu::BindGroup GetColorPipelineBindGroup0() const { return m_ColorPipelineBindGroup0; }

    wgpu::BindGroup GetTransformPipelineBindGroup0() const { return m_TransformPipelineBindGroup0; }

    Result<> UpdateWorldTransform(const Level::NodeHandle nodeHandle,
        const Mat44f& worldTransform);

    // Sync updates from CPU -< GPU.
    Result<> SyncToGpu();

private:
    // Maps a node handle to the index of its world transform in the world transform buffer.
    struct TransformBufferIndex
    {
        Level::NodeHandle NodeHandle;
        size_t Index;
    };

    Scene(const PropKit* propKit,
        WorldTransformBuffer worldTransformBuffer,
        DrawIndirectBuffer drawIndirectBuffer,
        MeshPropertiesBuffer meshPropertiesBuffer,
        CameraParamsBuffer cameraParamsBuffer,
        wgpu::BindGroup colorPipelineBindGroup0,
        wgpu::BindGroup transformPipelineBindGroup0,
        std::vector<ModelInstance>&& modelInstances,
        std::vector<ShaderInterop::WorldTransform>&& worldTransforms,
        std::vector<TransformBufferIndex>&& transformBufferIndices);

    Result<size_t> GetTransformBufferIndex(const Level::NodeHandle& nodeHandle) const;

    const PropKit* m_PropKit;
    WorldTransformBuffer m_WorldTransformBuffer;
    DrawIndirectBuffer m_DrawIndirectBuffer;
    MeshPropertiesBuffer m_MeshPropertiesBuffer;
    CameraParamsBuffer m_CameraParamsBuffer;
    wgpu::BindGroup m_ColorPipelineBindGroup0;
    wgpu::BindGroup m_TransformPipelineBindGroup0;
    std::vector<ModelInstance> m_ModelInstances;

    // Staging buffer for world transforms.
    std::vector<ShaderInterop::WorldTransform> m_WorldTransforms;

    // Sorted by node handle to allow binary search by node handle.
    std::vector<TransformBufferIndex> m_TransformBufferIndices;
};