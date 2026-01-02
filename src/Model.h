#pragma once

#include "Mesh.h"

#include <vector>

/// @brief Instance of a mesh within a model.
class MeshInstance
{
public:
    const int MeshIndex = -1;
    const int ParentIndex = -1;
    const Mat44f Transform{ 1.0f };
};

/// @brief Specification for creating a model.
class ModelSpec
{
public:

    ModelSpec(
        std::vector<MeshSpec>&& meshSpecs,
        std::vector<MeshInstance>&& meshInstances);

    /// @brief List of mesh specifications that make up the model.
    const std::vector<MeshSpec> MeshSpecs;
    /// @brief List of mesh instances that make up the model.
    const std::vector<MeshInstance> MeshInstances;

private:
        ModelSpec() = delete;
};

class Model
{
public:

    static Result<RefPtr<Model>> Create(
        std::vector<Mesh>&& meshes,
        std::vector<MeshInstance>&& meshInstances);

    const std::vector<Mesh> Meshes;
    const std::vector<MeshInstance> MeshInstances;

private:

    Model() = delete;

    explicit Model(std::vector<Mesh>&& meshes, std::vector<MeshInstance>&& meshInstances);

    IMPLEMENT_REFCOUNT(Model);
};