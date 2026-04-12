#pragma once

#include "Mesh.h"

#include "SceneKit.h"

#include <limits>

static inline const TransformIndex kInvalidTransformIndex = std::numeric_limits<TransformIndex>::max();

/// @brief Node representing a transform in a model's hierarchy.
/// Mesh instances reference these nodes for their transforms.
/// Multiple mesh instances can reference the same node.
class TransformNode
{
public:
    const TransformIndex ParentIndex = kInvalidTransformIndex;
    const Mat44f Transform{ 1.0f };
};

/// @brief Specification for creating a model.
class ModelSpec
{
public:

    ModelSpec(
        const imvector<MeshSpec>& meshSpecs,
        const imvector<TransformIndex>& meshToTransformMapping,
        const imvector<TransformNode>& transformNodes);

        const imvector<MeshSpec>& GetMeshSpecs() const { return m_MeshSpecs; }
        const imvector<TransformIndex>& GetMeshToTransformMapping() const { return m_MeshToTransformMapping; }
        const imvector<TransformNode>& GetTransformNodes() const { return m_TransformNodes; }
private:
        ModelSpec() = delete;

    /// @brief List of mesh specifications that make up the model.
    imvector<MeshSpec> m_MeshSpecs;
    /// @brief Mapping from meshes to transforms.
    imvector<TransformIndex> m_MeshToTransformMapping;
    /// @brief List of transform nodes that make up the model.
    imvector<TransformNode> m_TransformNodes;
};

class Model
{
public:

    Model() = default;
    ~Model();
    Model(const Model& other) = delete;
    Model& operator=(const Model& other) = delete;
    Model(Model&& other) noexcept
    {
        m_Meshes = std::move(other.m_Meshes);
        m_GpuDevice = std::move(other.m_GpuDevice);
        m_TransformBuffer = std::move(other.m_TransformBuffer);
        m_MeshToTransformMapping = std::move(other.m_MeshToTransformMapping);
        m_DrawIndirectBuffer = std::move(other.m_DrawIndirectBuffer);
        m_VertexBuffer = std::move(other.m_VertexBuffer);
        m_IndexBuffer = std::move(other.m_IndexBuffer);

        other.m_GpuDevice = nullptr;
        other.m_TransformBuffer = nullptr;
        other.m_MeshToTransformMapping = nullptr;
        other.m_DrawIndirectBuffer = nullptr;
        other.m_VertexBuffer = nullptr;
        other.m_IndexBuffer = nullptr;
    }

    /// Disable move assignment because it will leak vertex/index buffers.
    Model& operator=(Model&& other) = delete;

    static Result<Model> Create(
        const imvector<Mesh>& meshes,
        GpuDevice* gpuDevice,
        GpuStorageBuffer* transformBuffer,
        GpuStorageBuffer* meshToTransformMapping,
        GpuDrawIndirectBuffer* drawIndirectBuffer,
        GpuVertexBuffer* vertexBuffer,
        GpuIndexBuffer* indexBuffer);

    const imvector<Mesh>& GetMeshes() const { return m_Meshes; }
    const GpuStorageBuffer* GetTransformBuffer() const { return m_TransformBuffer; }
    const GpuStorageBuffer* GetMeshToTransformMapping() const { return m_MeshToTransformMapping; }
    const GpuDrawIndirectBuffer* GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }
    const GpuVertexBuffer* GetGpuVertexBuffer() const { return m_VertexBuffer; }
    const GpuIndexBuffer* GetGpuIndexBuffer() const { return m_IndexBuffer; }

private:
    Model(const imvector<Mesh>& meshes,
        GpuDevice* gpuDevice,
        GpuStorageBuffer* transformBuffer,
        GpuStorageBuffer* meshToTransformMapping,
        GpuDrawIndirectBuffer* drawIndirectBuffer,
        GpuVertexBuffer* vertexBuffer,
        GpuIndexBuffer* indexBuffer)
        : m_Meshes(meshes),
          m_GpuDevice(gpuDevice),
          m_TransformBuffer(transformBuffer),
          m_MeshToTransformMapping(meshToTransformMapping),
          m_DrawIndirectBuffer(drawIndirectBuffer),
          m_VertexBuffer(vertexBuffer),
          m_IndexBuffer(indexBuffer)
    {
    }

    imvector<Mesh> m_Meshes;

    GpuDevice* m_GpuDevice{nullptr};
    GpuStorageBuffer* m_TransformBuffer{nullptr};
    GpuStorageBuffer* m_MeshToTransformMapping{nullptr};
    GpuDrawIndirectBuffer* m_DrawIndirectBuffer{nullptr};
    GpuVertexBuffer* m_VertexBuffer{nullptr};
    GpuIndexBuffer* m_IndexBuffer{nullptr};
};