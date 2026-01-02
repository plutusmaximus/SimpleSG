#pragma once

#include "Mesh.h"

#include <vector>

class ModelSpec
{
public:

    const std::vector<MeshSpec> MeshSpecs;
};

class Model
{
public:

    static Result<RefPtr<Model>> Create(std::vector<Mesh>&& meshes);

    const std::vector<Mesh> Meshes;

private:

    Model() = delete;

    explicit Model(std::vector<Mesh>&& meshes)
        : Meshes(std::move(meshes))
    {
    };

    IMPLEMENT_REFCOUNT(Model);
};