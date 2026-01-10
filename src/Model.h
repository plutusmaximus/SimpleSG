#pragma once

#include "Mesh.h"

#include <vector>

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

    /// @brief List of mesh specifications that make up the model.
    const imvector<MeshSpec> MeshSpecs;
    /// @brief List of mesh instances that make up the model.
    const imvector<MeshInstance> MeshInstances;
    /// @brief List of transform nodes that make up the model.
    const imvector<TransformNode> TransformNodes;
private:
        ModelSpec() = delete;
};

class Model
{
public:

    Model() = default;

    static Result<Model> Create(
        const imvector<Mesh> meshes,
        const imvector<MeshInstance> meshInstances,
        const imvector<TransformNode> transformNodes);

    const imvector<Mesh> GetMeshes() const { return m_Data->Meshes; }
    const imvector<MeshInstance> GetMeshInstances() const { return m_Data->MeshInstances; }
    const imvector<TransformNode> GetTransformNodes() const { return m_Data->TransformNodes; }

private:

    struct Data
    {
        Data(const imvector<Mesh>& meshes, const imvector<MeshInstance>& meshInstances, const imvector<TransformNode>& transformNodes)
            : Meshes(meshes), MeshInstances(meshInstances), TransformNodes(transformNodes) {}
        const imvector<Mesh> Meshes;
        const imvector<MeshInstance> MeshInstances;
        const imvector<TransformNode> TransformNodes;
        IMPLEMENT_REFCOUNT(Data);
    };

    explicit Model(RefPtr<Data> data)
        : m_Data(std::move(data))
    {
    }

    RefPtr<Data> m_Data;
};