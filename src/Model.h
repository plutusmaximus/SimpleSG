#pragma once

#include "Mesh.h"

#include <limits>

class GpuDevice;
class GpuVertexBuffer;
class GpuIndexBuffer;

using TransformIndex = uint32_t;

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
        m_MeshToTransformMapping = std::move(other.m_MeshToTransformMapping);
        m_TransformNodes = std::move(other.m_TransformNodes);
        m_GpuDevice = other.m_GpuDevice;
        m_VertexBuffer = other.m_VertexBuffer;
        m_IndexBuffer = other.m_IndexBuffer;

        other.m_GpuDevice = nullptr;
        other.m_VertexBuffer = nullptr;
        other.m_IndexBuffer = nullptr;
    }

    /// Disable move assignment because it will leak vertex/index buffers.
    Model& operator=(Model&& other) = delete;

    static Result<Model> Create(
        const imvector<Mesh>& meshes,
        const imvector<TransformNode>& transformNodes,
        GpuDevice* gpuDevice,
        GpuReadonlyBuffer* meshToTransformMapping,
        GpuVertexBuffer* vertexBuffer,
        GpuIndexBuffer* indexBuffer);

    const imvector<Mesh>& GetMeshes() const { return m_Meshes; }
    const imvector<TransformNode>& GetTransformNodes() const { return m_TransformNodes; }
    const GpuReadonlyBuffer* GetMeshToTransformMapping() const { return m_MeshToTransformMapping; }
    const GpuVertexBuffer* GetGpuVertexBuffer() const { return m_VertexBuffer; }
    const GpuIndexBuffer* GetGpuIndexBuffer() const { return m_IndexBuffer; }

private:
    Model(const imvector<Mesh>& meshes,
        const imvector<TransformNode>& transformNodes,
        GpuDevice* gpuDevice,
        GpuReadonlyBuffer* meshToTransformMapping,
        GpuVertexBuffer* vertexBuffer,
        GpuIndexBuffer* indexBuffer)
        : m_Meshes(meshes),
          m_TransformNodes(transformNodes),
          m_GpuDevice(gpuDevice),
          m_MeshToTransformMapping(meshToTransformMapping),
          m_VertexBuffer(vertexBuffer),
          m_IndexBuffer(indexBuffer)
    {
    }

    imvector<Mesh> m_Meshes;
    imvector<TransformNode> m_TransformNodes;

    GpuDevice* m_GpuDevice{nullptr};
    GpuReadonlyBuffer* m_MeshToTransformMapping{nullptr};
    GpuVertexBuffer* m_VertexBuffer{nullptr};
    GpuIndexBuffer* m_IndexBuffer{nullptr};
};