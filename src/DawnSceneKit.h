#pragma once

#include "SceneKit.h"
#include "VecMath.h"

#include <webgpu/webgpu_cpp.h>

#include <span>
#include <vector>

class DawnSceneKit : public SceneKit
{
public:

    class Builder
    {
    public:

        Builder& SetIndexBuffer(wgpu::Buffer indexBuffer)
        {
            m_IndexBuffer = indexBuffer;
            return *this;
        }

        Builder& SetVertexBuffer(wgpu::Buffer vertexBuffer)
        {
            m_VertexBuffer = vertexBuffer;
            return *this;
        }

        Builder& SetTransformBuffer(wgpu::Buffer transformBuffer)
        {
            m_TransformBuffer = transformBuffer;
            return *this;
        }

        Builder& SetMaterialConstantsBuffer(wgpu::Buffer materialConstantsBuffer)
        {
            m_MaterialConstantsBuffer = materialConstantsBuffer;
            return *this;
        }

        Builder& SetDrawIndirectBuffer(wgpu::Buffer drawIndirectBuffer)
        {
            m_DrawIndirectBuffer = drawIndirectBuffer;
            return *this;
        }

        Builder& SetMeshInstanceBuffer(wgpu::Buffer meshInstanceBuffer)
        {
            m_MeshInstanceBuffer = meshInstanceBuffer;
            return *this;
        }

        Builder& SetColorPipelineBindGroup0(wgpu::BindGroup colorPipelineBindGroup0)
        {
            m_ColorPipelineBindGroup0 = colorPipelineBindGroup0;
            return *this;
        }

        Builder& SetTransformPipelineBindGroup0(wgpu::BindGroup transformPipelineBindGroup0)
        {
            m_TransformPipelineBindGroup0 = transformPipelineBindGroup0;
            return *this;
        }

        Builder& SetMaterialBindGroups(std::vector<wgpu::BindGroup>&& materialBindGroups)
        {
            m_MaterialBindGroups = std::move(materialBindGroups);
            return *this;
        }

        Builder& SetMaterialIndices(std::vector<MaterialIndex>&& materialIndices)
        {
            m_MaterialIndices = std::move(materialIndices);
            return *this;
        }

        DawnSceneKit Build()
        {
            MLG_ASSERT(Validate(), "DawnSceneKit::Builder is not in a valid state to build a DawnSceneKit");

            return DawnSceneKit(
                m_IndexBuffer,
                m_VertexBuffer,
                m_TransformBuffer,
                m_MaterialConstantsBuffer,
                m_DrawIndirectBuffer,
                m_MeshInstanceBuffer,
                m_ColorPipelineBindGroup0,
                m_TransformPipelineBindGroup0,
                std::move(m_MaterialBindGroups),
                std::move(m_MaterialIndices));
        }

    private:

        bool Validate() const
        {
            return m_IndexBuffer && m_VertexBuffer && m_TransformBuffer &&
                   m_MaterialConstantsBuffer && m_DrawIndirectBuffer && m_MeshInstanceBuffer &&
                   m_ColorPipelineBindGroup0 && m_TransformPipelineBindGroup0;
        }

        wgpu::Buffer m_IndexBuffer{nullptr};
        wgpu::Buffer m_VertexBuffer{nullptr};
        wgpu::Buffer m_TransformBuffer{nullptr};
        wgpu::Buffer m_DrawIndirectBuffer{nullptr};
        wgpu::Buffer m_MeshInstanceBuffer{nullptr};
        wgpu::Buffer m_MaterialConstantsBuffer{nullptr};
        wgpu::BindGroup m_ColorPipelineBindGroup0{nullptr};
        wgpu::BindGroup m_TransformPipelineBindGroup0{nullptr};
        std::vector<wgpu::BindGroup> m_MaterialBindGroups;
        std::vector<MaterialIndex> m_MaterialIndices;
    };

    DawnSceneKit() = default;
    DawnSceneKit(const DawnSceneKit&) = delete;
    DawnSceneKit& operator=(const DawnSceneKit&) = delete;
    DawnSceneKit(DawnSceneKit&&) = delete;
    DawnSceneKit& operator=(DawnSceneKit&&) = delete;

    ~DawnSceneKit() override = default;

    DawnSceneKit(wgpu::Buffer indexBuffer,
        wgpu::Buffer vertexBuffer,
        wgpu::Buffer transformBuffer,
        wgpu::Buffer materialConstantsBuffer,
        wgpu::Buffer drawIndirectBuffer,
        wgpu::Buffer meshInstanceBuffer,
        wgpu::BindGroup colorPipelineBindGroup0,
        wgpu::BindGroup transformPipelineBindGroup0,
        std::vector<wgpu::BindGroup>&& materialBindGroups,
        std::vector<MaterialIndex>&& materialIndices)
        : m_IndexBuffer(indexBuffer),
          m_VertexBuffer(vertexBuffer),
          m_TransformBuffer(transformBuffer),
          m_MaterialConstantsBuffer(materialConstantsBuffer),
          m_DrawIndirectBuffer(drawIndirectBuffer),
          m_MeshInstanceBuffer(meshInstanceBuffer),
          m_ColorPipelineBindGroup0(colorPipelineBindGroup0),
          m_TransformPipelineBindGroup0(transformPipelineBindGroup0),
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

    wgpu::BindGroup GetColorPipelineBindGroup0() const { return m_ColorPipelineBindGroup0; }
    wgpu::BindGroup GetTransformPipelineBindGroup0() const { return m_TransformPipelineBindGroup0; }

private:

    wgpu::Buffer m_IndexBuffer{nullptr};
    wgpu::Buffer m_VertexBuffer{nullptr};
    wgpu::Buffer m_TransformBuffer{nullptr};
    wgpu::Buffer m_DrawIndirectBuffer{nullptr};
    wgpu::Buffer m_MeshInstanceBuffer{nullptr};
    wgpu::Buffer m_MaterialConstantsBuffer{nullptr};
    wgpu::BindGroup m_ColorPipelineBindGroup0{nullptr};
    wgpu::BindGroup m_TransformPipelineBindGroup0{nullptr};

    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
    std::vector<MaterialIndex> m_MaterialIndices;
};