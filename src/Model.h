#pragma once

#include "Mesh.h"

class GpuDevice;
class GpuVertexBuffer;
class GpuIndexBuffer;

/// @brief Node representing a transform in a model's hierarchy.
/// Mesh instances reference these nodes for their transforms.
/// Multiple mesh instances can reference the same node.
class TransformNode
{
public:
    const int ParentIndex = -1;
    const Mat44f Transform{ 1.0f };
};

/// @brief Instance of a mesh within a model.
class MeshInstance
{
public:
    /// @brief Index of the mesh in the model's mesh list.
    const int MeshIndex = -1;
    /// @brief Index of the transform node in the model's transform node list.
    const int NodeIndex = -1;
};

/// @brief Specification for creating a model.
class ModelSpec
{
public:

    ModelSpec(
        const imvector<MeshSpec>& meshSpecs,
        const imvector<MeshInstance>& meshInstances,
        const imvector<TransformNode>& transformNodes);

        const imvector<MeshSpec>& GetMeshSpecs() const { return m_MeshSpecs; }
        const imvector<MeshInstance>& GetMeshInstances() const { return m_MeshInstances; }
        const imvector<TransformNode>& GetTransformNodes() const { return m_TransformNodes; }
private:
        ModelSpec() = delete;

    /// @brief List of mesh specifications that make up the model.
    imvector<MeshSpec> m_MeshSpecs;
    /// @brief List of mesh instances that make up the model.
    imvector<MeshInstance> m_MeshInstances;
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
        m_MeshInstances = std::move(other.m_MeshInstances);
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
        const imvector<MeshInstance>& meshInstances,
        const imvector<TransformNode>& transformNodes,
        GpuDevice* gpuDevice,
        GpuVertexBuffer* vertexBuffer,
        GpuIndexBuffer* indexBuffer);

    const imvector<Mesh>& GetMeshes() const { return m_Meshes; }
    const imvector<MeshInstance>& GetMeshInstances() const { return m_MeshInstances; }
    const imvector<TransformNode>& GetTransformNodes() const { return m_TransformNodes; }

private:
    Model(const imvector<Mesh>& meshes,
        const imvector<MeshInstance>& meshInstances,
        const imvector<TransformNode>& transformNodes,
        GpuDevice* gpuDevice,
        GpuVertexBuffer* vertexBuffer,
        GpuIndexBuffer* indexBuffer)
        : m_Meshes(meshes),
          m_MeshInstances(meshInstances),
          m_TransformNodes(transformNodes),
          m_GpuDevice(gpuDevice),
          m_VertexBuffer(vertexBuffer),
          m_IndexBuffer(indexBuffer)
    {
    }

    imvector<Mesh> m_Meshes;
    imvector<MeshInstance> m_MeshInstances;
    imvector<TransformNode> m_TransformNodes;

    GpuDevice* m_GpuDevice = nullptr;
    GpuVertexBuffer* m_VertexBuffer = nullptr;
    GpuIndexBuffer* m_IndexBuffer = nullptr;
};