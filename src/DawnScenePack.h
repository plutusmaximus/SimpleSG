#pragma once

#include "ScenePack.h"
#include "VecMath.h"

#include <webgpu/webgpu_cpp.h>

#include <span>
#include <vector>

struct MaterialBinding
{
    wgpu::Texture BaseTexture;
    wgpu::Sampler Sampler;
    wgpu::Buffer ConstantsBuffer;
    wgpu::BindGroup BindGroup;
};

class DrawIndirectBufferParams
{
public:
    uint32_t IndexCount;
    uint32_t InstanceCount;
    uint32_t FirstIndex;
    uint32_t BaseVertex;
    uint32_t FirstInstance;
};

class DawnScenePack : public ScenePack
{
public:

    DawnScenePack() = default;
    DawnScenePack(const DawnScenePack&) = delete;
    DawnScenePack& operator=(const DawnScenePack&) = delete;
    DawnScenePack(DawnScenePack&&) = delete;
    DawnScenePack& operator=(DawnScenePack&&) = delete;

    ~DawnScenePack() override = default;

    DawnScenePack(wgpu::Buffer indexBuffer,
        wgpu::Buffer vertexBuffer,
        wgpu::Buffer transformBuffer,
        wgpu::Buffer drawIndirectBuffer,
        wgpu::Buffer meshToTransformMappingBuffer,
        wgpu::BindGroup colorRenderBindGroup0,
        wgpu::BindGroup transformBindGroup0,
        std::vector<MaterialBinding>&& materialBindings,
        std::vector<uint32_t>&& meshToMaterialMap)
        : m_IndexBuffer(indexBuffer),
          m_VertexBuffer(vertexBuffer),
          m_TransformBuffer(transformBuffer),
          m_MeshToTransformMappingBuffer(meshToTransformMappingBuffer),
          m_DrawIndirectBuffer(drawIndirectBuffer),
          m_ColorRenderBindGroup0(colorRenderBindGroup0),
          m_TransformBindGroup0(transformBindGroup0),
          m_MaterialBindings(std::move(materialBindings)),
          m_MeshToMaterialMap(std::move(meshToMaterialMap))

    {
    }

    uint32_t GetTransformCount() const override
    {
        if (m_TransformBuffer)
        {
            return static_cast<uint32_t>(m_TransformBuffer.GetSize() / sizeof(Mat44f));
        }
        return 0;
    }

    uint32_t GetMeshCount() const override
    {
        if (m_DrawIndirectBuffer)
        {
            return static_cast<uint32_t>(m_DrawIndirectBuffer.GetSize() / sizeof(DrawIndirectBufferParams));
        }
        return 0;
    }

    const std::span<const MaterialBinding> GetMaterialBindings() const
    {
        return m_MaterialBindings;
    }

    const std::span<const uint32_t> GetMeshToMaterialMap() const
    {
        return m_MeshToMaterialMap;
    }

    wgpu::Buffer GetTransformBuffer() const { return m_TransformBuffer; }
    wgpu::Buffer GetMeshToTransformMappingBuffer() const { return m_MeshToTransformMappingBuffer; }
    wgpu::Buffer GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }
    wgpu::Buffer GetVertexBuffer() const { return m_VertexBuffer; }
    wgpu::Buffer GetIndexBuffer() const { return m_IndexBuffer; }

    wgpu::BindGroup GetColorRenderBindGroup0() const { return m_ColorRenderBindGroup0; }
    wgpu::BindGroup GetTransformBindGroup0() const { return m_TransformBindGroup0; }

private:

    wgpu::Buffer m_IndexBuffer{nullptr};
    wgpu::Buffer m_VertexBuffer{nullptr};
    wgpu::Buffer m_TransformBuffer{nullptr};
    wgpu::Buffer m_DrawIndirectBuffer{nullptr};
    wgpu::Buffer m_MeshToTransformMappingBuffer{nullptr};

    wgpu::BindGroup m_ColorRenderBindGroup0{nullptr};
    wgpu::BindGroup m_TransformBindGroup0{nullptr};

    std::vector<MaterialBinding> m_MaterialBindings;
    std::vector<uint32_t> m_MeshToMaterialMap;
};