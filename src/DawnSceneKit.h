#pragma once

#include "SceneKit.h"
#include "VecMath.h"

#include <webgpu/webgpu_cpp.h>

#include <span>
#include <vector>

class DrawIndirectBufferParams
{
public:
    uint32_t IndexCount;
    uint32_t InstanceCount;
    uint32_t FirstIndex;
    uint32_t BaseVertex;
    uint32_t FirstInstance;
};

class DawnSceneKit : public SceneKit
{
public:

    DawnSceneKit() = default;
    DawnSceneKit(const DawnSceneKit&) = delete;
    DawnSceneKit& operator=(const DawnSceneKit&) = delete;
    DawnSceneKit(DawnSceneKit&&) = delete;
    DawnSceneKit& operator=(DawnSceneKit&&) = delete;

    ~DawnSceneKit() override = default;

    DawnSceneKit(wgpu::Buffer indexBuffer,
        wgpu::Buffer vertexBuffer,
        wgpu::Buffer transformBuffer,
        wgpu::Buffer drawIndirectBuffer,
        wgpu::Buffer transformIndexBuffer,
        wgpu::BindGroup colorRenderBindGroup0,
        wgpu::BindGroup transformBindGroup0,
        std::vector<wgpu::BindGroup>&& materialBindGroups,
        std::vector<uint32_t>&& materialIndices)
        : m_IndexBuffer(indexBuffer),
          m_VertexBuffer(vertexBuffer),
          m_TransformBuffer(transformBuffer),
          m_TransformIndexBuffer(transformIndexBuffer),
          m_DrawIndirectBuffer(drawIndirectBuffer),
          m_ColorRenderBindGroup0(colorRenderBindGroup0),
          m_TransformBindGroup0(transformBindGroup0),
          m_MaterialBindGroups(std::move(materialBindGroups)),
          m_MaterialIndices(std::move(materialIndices))

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

    const std::span<const wgpu::BindGroup> GetMaterialBindGroups() const
    {
        return m_MaterialBindGroups;
    }

    const std::span<const uint32_t> GetMaterialIndices() const
    {
        return m_MaterialIndices;
    }

    wgpu::Buffer GetTransformBuffer() const { return m_TransformBuffer; }
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
    wgpu::Buffer m_TransformIndexBuffer{nullptr};

    wgpu::BindGroup m_ColorRenderBindGroup0{nullptr};
    wgpu::BindGroup m_TransformBindGroup0{nullptr};

    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
    std::vector<uint32_t> m_MaterialIndices;
};