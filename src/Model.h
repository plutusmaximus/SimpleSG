#pragma once

#include "Mesh.h"
#include "Vertex.h"

#include <vector>
#include <span>

class ModelSpec
{
public:
    const std::span<const Vertex> Vertices;
    const std::span<const VertexIndex> Indices;
    const std::span<const MeshSpec> MeshSpecs;
};

class Model
{
public:

    class Meshes : private std::vector<Mesh>
    {
        friend class Model;
    public:

        using iterator = std::vector<RefPtr<Mesh>>::iterator;
        using const_iterator = std::vector<RefPtr<Mesh>>::const_iterator;

        using std::vector<Mesh>::vector;
        using std::vector<Mesh>::begin;
        using std::vector<Mesh>::end;
    };

    static Result<RefPtr<Model>> Create(std::span<Mesh> meshes);

    Meshes const Meshes;

private:

    Model() = delete;

    Model(std::span<Mesh> meshes);

    IMPLEMENT_REFCOUNT(Model);
};