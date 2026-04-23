#pragma once

#include "shaders/ShaderTypes.h"
#include "WebgpuHelper.h"

#include <limits>
#include <span>
#include <vector>

// Strongly-typed GPU storage buffer classes.
using TransformBuffer = TypedGpuBuffer<ShaderTypes::MeshTransform>;
using MeshPropertiesBuffer = TypedGpuBuffer<ShaderTypes::MeshProperties>;

struct TransformDef
{
    static constexpr TransformIndex kInvalidParentIndex = std::numeric_limits<TransformIndex>::max();

    Mat44f Transform;
    TransformIndex ParentIndex{ kInvalidParentIndex };
};

struct ModelInstance
{
    ModelIndex ModelIndex;
    TransformIndex TransformIndex;
};

struct SceneDef
{
    std::vector<TransformDef> TransformDefs;
    std::vector<ModelInstance> ModelInstances;
};

class PropKit;

class Scene
{
public:
    static Result<> Create(const SceneDef& sceneDef, const PropKit& propKit, Scene& outScene);

    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&& other) = default;
    Scene& operator=(Scene&& other) = default;

    const std::span<const ModelInstance> GetModelInstances() const { return m_ModelInstances; }

    TransformBuffer GetTransformBuffer() const { return m_TransformBuffer; }

    uint32_t GetTransformCount() const
    {
        if(m_TransformBuffer)
        {
            return static_cast<uint32_t>(m_TransformBuffer.GetSize() / sizeof(Mat44f));
        }
        return 0;
    }

    IndirectBuffer GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }

    wgpu::BindGroup GetColorPipelineBindGroup0() const { return m_ColorPipelineBindGroup0; }

    wgpu::BindGroup GetTransformPipelineBindGroup0() const { return m_TransformPipelineBindGroup0; }

private:

    Scene(const PropKit* propKit,
        TransformBuffer transformBuffer,
        IndirectBuffer drawIndirectBuffer,
        MeshPropertiesBuffer meshPropertiesBuffer,
        wgpu::BindGroup colorPipelineBindGroup0,
        wgpu::BindGroup transformPipelineBindGroup0,
        std::vector<ModelInstance>&& modelInstances)
        : m_PropKit(propKit),
          m_TransformBuffer(transformBuffer),
          m_DrawIndirectBuffer(drawIndirectBuffer),
          m_MeshPropertiesBuffer(meshPropertiesBuffer),
          m_ColorPipelineBindGroup0(colorPipelineBindGroup0),
          m_TransformPipelineBindGroup0(transformPipelineBindGroup0),
          m_ModelInstances(std::move(modelInstances))
    {
    }

    const PropKit* m_PropKit;
    TransformBuffer m_TransformBuffer;
    IndirectBuffer m_DrawIndirectBuffer;
    MeshPropertiesBuffer m_MeshPropertiesBuffer;
    wgpu::BindGroup m_ColorPipelineBindGroup0;
    wgpu::BindGroup m_TransformPipelineBindGroup0;
    std::vector<ModelInstance> m_ModelInstances;
};