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
        std::vector<MeshSpec>&& meshSpecs,
        std::vector<MeshInstance>&& meshInstances,
        std::vector<TransformNode>&& transformNodes);

    /// @brief List of mesh specifications that make up the model.
    const std::vector<MeshSpec> MeshSpecs;
    /// @brief List of mesh instances that make up the model.
    const std::vector<MeshInstance> MeshInstances;
    /// @brief List of transform nodes that make up the model.
    const std::vector<TransformNode> TransformNodes;

private:
        ModelSpec() = delete;
};

class Model
{
public:

    static Result<RefPtr<Model>> Create(
        std::vector<Mesh>&& meshes,
        std::vector<MeshInstance>&& meshInstances,
        std::vector<TransformNode>&& transformNodes);

    const std::vector<Mesh> Meshes;
    const std::vector<MeshInstance> MeshInstances;
    const std::vector<TransformNode> TransformNodes;

private:

    Model() = delete;

    explicit Model(
        std::vector<Mesh>&& meshes,
        std::vector<MeshInstance>&& meshInstances,
        std::vector<TransformNode>&& transformNodes);

    IMPLEMENT_REFCOUNT(Model);
};