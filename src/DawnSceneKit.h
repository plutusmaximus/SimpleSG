#pragma once

#include "Result.h"
#include "SceneKit.h"
#include "VecMath.h"
#include "WebgpuHelper.h"

#include <filesystem>
#include <span>
#include <vector>

class DawnSceneKit : public SceneKit
{
public:
    static Result<DawnSceneKit*> Create(wgpu::Device& wgpuDevice,
        const std::filesystem::path& rootPath,
        const SceneKitSourceData& sceneKitData);

    static void Destroy(DawnSceneKit* sceneKit);

    class Builder
    {
    public:

        Builder& SetIndexBuffer(IndexBuffer indexBuffer)
        {
            m_IndexBuffer = indexBuffer;
            return *this;
        }

        Builder& SetVertexBuffer(VertexBuffer vertexBuffer)
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

        Builder& SetMeshDrawDataBuffer(wgpu::Buffer meshDrawDataBuffer)
        {
            m_MeshDrawDataBuffer = meshDrawDataBuffer;
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

        Builder& SetMeshes(std::vector<MeshProperties>&& meshes)
        {
            m_Meshes = std::move(meshes);
#ifndef NDEBUG
            for(const auto& mesh : m_Meshes)
            {
                const Vec3f& aabbMax = mesh.BoundingBox.GetMax();
                const Vec3f& aabbMin = mesh.BoundingBox.GetMin();
                MLG_ASSERT(aabbMin.x <= aabbMax.x &&
                           aabbMin.y <= aabbMax.y &&
                           aabbMin.z <= aabbMax.z,
                    "Mesh has invalid bounding box");
            }
#endif // NDEBUG
            return *this;
        }

        Builder& SetModelInstances(std::vector<ModelInstance>&& modelInstances)
        {
            m_ModelInstances = std::move(modelInstances);
            return *this;
        }

        DawnSceneKit* Build()
        {
            MLG_ASSERT(Validate(), "DawnSceneKit::Builder is not in a valid state to build a DawnSceneKit");

            return new DawnSceneKit(
                m_IndexBuffer,
                m_VertexBuffer,
                m_TransformBuffer,
                m_MaterialConstantsBuffer,
                m_DrawIndirectBuffer,
                m_MeshDrawDataBuffer,
                m_ColorPipelineBindGroup0,
                m_TransformPipelineBindGroup0,
                std::move(m_MaterialBindGroups),
                std::move(m_Meshes),
                std::move(m_ModelInstances));
        }

    private:

        bool Validate() const
        {
            return m_IndexBuffer && m_VertexBuffer && m_TransformBuffer &&
                   m_MaterialConstantsBuffer && m_DrawIndirectBuffer && m_MeshDrawDataBuffer &&
                   m_ColorPipelineBindGroup0 && m_TransformPipelineBindGroup0 &&
                   m_Meshes.size() > 0 && m_ModelInstances.size() > 0;
        }

        IndexBuffer m_IndexBuffer{nullptr};
        VertexBuffer m_VertexBuffer{nullptr};
        wgpu::Buffer m_TransformBuffer{nullptr};
        wgpu::Buffer m_DrawIndirectBuffer{nullptr};
        wgpu::Buffer m_MeshDrawDataBuffer{nullptr};
        wgpu::Buffer m_MaterialConstantsBuffer{nullptr};
        wgpu::BindGroup m_ColorPipelineBindGroup0{nullptr};
        wgpu::BindGroup m_TransformPipelineBindGroup0{nullptr};
        std::vector<wgpu::BindGroup> m_MaterialBindGroups;
        std::vector<MeshProperties> m_Meshes;
        std::vector<ModelInstance> m_ModelInstances;
    };

    DawnSceneKit() = default;
    DawnSceneKit(const DawnSceneKit&) = delete;
    DawnSceneKit& operator=(const DawnSceneKit&) = delete;
    DawnSceneKit(DawnSceneKit&&) = delete;
    DawnSceneKit& operator=(DawnSceneKit&&) = delete;

    ~DawnSceneKit() override = default;

    DawnSceneKit(IndexBuffer indexBuffer,
        VertexBuffer vertexBuffer,
        wgpu::Buffer transformBuffer,
        wgpu::Buffer materialConstantsBuffer,
        wgpu::Buffer drawIndirectBuffer,
        wgpu::Buffer meshDrawDataBuffer,
        wgpu::BindGroup colorPipelineBindGroup0,
        wgpu::BindGroup transformPipelineBindGroup0,
        std::vector<wgpu::BindGroup>&& materialBindGroups,
        std::vector<MeshProperties>&& meshes,
        std::vector<ModelInstance>&& modelInstances)
        : m_IndexBuffer(indexBuffer),
          m_VertexBuffer(vertexBuffer),
          m_TransformBuffer(transformBuffer),
          m_MaterialConstantsBuffer(materialConstantsBuffer),
          m_DrawIndirectBuffer(drawIndirectBuffer),
          m_MeshDrawDataBuffer(meshDrawDataBuffer),
          m_ColorPipelineBindGroup0(colorPipelineBindGroup0),
          m_TransformPipelineBindGroup0(transformPipelineBindGroup0),
          m_MaterialBindGroups(std::move(materialBindGroups)),
          m_Meshes(std::move(meshes)),
          m_ModelInstances(std::move(modelInstances))
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
        return static_cast<uint32_t>(m_Meshes.size());
    }

    const std::span<const wgpu::BindGroup> GetMaterialBindGroups() const
    {
        return m_MaterialBindGroups;
    }

    const std::span<const MeshProperties> GetMeshes() const
    {
        return m_Meshes;
    }

    const std::span<const ModelInstance> GetModelInstances() const
    {
        return m_ModelInstances;
    }

    wgpu::Buffer GetTransformBuffer() const { return m_TransformBuffer; }
    wgpu::Buffer GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }
    VertexBuffer GetVertexBuffer() const { return m_VertexBuffer; }
    IndexBuffer GetIndexBuffer() const { return m_IndexBuffer; }

    wgpu::BindGroup GetColorPipelineBindGroup0() const { return m_ColorPipelineBindGroup0; }
    wgpu::BindGroup GetTransformPipelineBindGroup0() const { return m_TransformPipelineBindGroup0; }

private:

    IndexBuffer m_IndexBuffer{nullptr};
    VertexBuffer m_VertexBuffer{nullptr};
    wgpu::Buffer m_TransformBuffer{nullptr};
    wgpu::Buffer m_DrawIndirectBuffer{nullptr};
    wgpu::Buffer m_MeshDrawDataBuffer{nullptr};
    wgpu::Buffer m_MaterialConstantsBuffer{nullptr};
    wgpu::BindGroup m_ColorPipelineBindGroup0{nullptr};
    wgpu::BindGroup m_TransformPipelineBindGroup0{nullptr};

    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
    std::vector<MeshProperties> m_Meshes;
    std::vector<ModelInstance> m_ModelInstances;
};