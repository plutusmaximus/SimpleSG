#pragma once

#include "Mesh.h"

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
    Model(const Model& other) = delete;
    Model& operator=(const Model& other) = delete;
    Model(Model&& other) = default;
    Model& operator=(Model&& other) = default;

    static Result<Model> Create(
        const imvector<Mesh>& meshes,
        const imvector<MeshInstance>& meshInstances,
        const imvector<TransformNode>& transformNodes);

    const imvector<Mesh>& GetMeshes() const { return m_Meshes; }
    const imvector<MeshInstance>& GetMeshInstances() const { return m_MeshInstances; }
    const imvector<TransformNode>& GetTransformNodes() const { return m_TransformNodes; }

private:

    Model(const imvector<Mesh>& meshes,
        const imvector<MeshInstance>& meshInstances,
        const imvector<TransformNode>& transformNodes)
        : m_Meshes(meshes),
          m_MeshInstances(meshInstances),
          m_TransformNodes(transformNodes)
    {
    }

    imvector<Mesh> m_Meshes;
    imvector<MeshInstance> m_MeshInstances;
    imvector<TransformNode> m_TransformNodes;
};