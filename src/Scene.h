#pragma once

#include "shaders/ShaderTypes.h"
#include "WebgpuHelper.h"

#include <limits>
#include <span>
#include <vector>

using TransformBuffer = TypedGpuBuffer<ShaderTypes::MeshTransform>;

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

class Scene
{
public:

    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&& other) = default;
    Scene& operator=(Scene&& other) = default;

    Scene(
        TransformBuffer transformBuffer,
        std::vector<ModelInstance>&& modelInstances,
        wgpu::BindGroup transformPipelineBindGroup0)
        : m_TransformBuffer(transformBuffer),
          m_ModelInstances(std::move(modelInstances)),
          m_TransformPipelineBindGroup0(transformPipelineBindGroup0)
    {
    }

    const std::span<const ModelInstance> GetModelInstances() const
    {
        return m_ModelInstances;
    }

    TransformBuffer GetTransformBuffer() const { return m_TransformBuffer; }

    uint32_t GetTransformCount() const
    {
        if (m_TransformBuffer)
        {
            return static_cast<uint32_t>(m_TransformBuffer.GetSize() / sizeof(Mat44f));
        }
        return 0;
    }

    wgpu::BindGroup GetTransformPipelineBindGroup0() const { return m_TransformPipelineBindGroup0; }

private:
    TransformBuffer m_TransformBuffer;
    std::vector<ModelInstance> m_ModelInstances;
    wgpu::BindGroup m_TransformPipelineBindGroup0{nullptr};
};