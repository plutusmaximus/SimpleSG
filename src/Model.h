#pragma once

#include "Mesh.h"
#include "SceneNode.h"
#include "SceneVisitor.h"
#include "Vertex.h"

#include <vector>
#include <span>

class ModelSpec
{
public:
    std::span<Vertex> Vertices;
    std::span<VertexIndex> Indices;
    std::span<MeshSpec> MeshSpecs;
};

class Model : public SceneNode
{
public:

    static Result<RefPtr<Model>> Create(std::span<RefPtr<Mesh>> meshes)
    {
        Model* model = new Model(meshes);

        expectv(model, "Error allocating model");

        return model;
    }

    class Meshes : private std::vector<RefPtr<Mesh>>
    {
        friend class Model;
    public:

        using iterator = std::vector<RefPtr<Mesh>>::iterator;
        using const_iterator = std::vector<RefPtr<Mesh>>::const_iterator;

        using std::vector<RefPtr<Mesh>>::begin;
        using std::vector<RefPtr<Mesh>>::end;
    };

    void Accept(SceneVisitor* visitor) override
    {
        visitor->Visit(this);
    }

    Meshes Meshes;

private:

    Model() = delete;

    Model(std::span<RefPtr<Mesh>> meshes)
    {
        Meshes.reserve(meshes.size());
        Meshes.assign(meshes.begin(), meshes.end());
    }
};