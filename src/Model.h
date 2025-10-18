#pragma once

#include "Mesh.h"
#include "SceneNode.h"
#include "SceneVisitor.h"

#include <vector>

class Model : public SceneNode
{
public:
    template<int COUNT>
    static RefPtr<Model> Create(const RefPtr<Mesh> (&meshes)[COUNT])
    {
        return Create(std::span<const RefPtr<Mesh>>(meshes));
    }

    static RefPtr<Model> Create(std::span<const RefPtr<Mesh>> meshes)
    {
        return new Model(meshes);
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

    Model(std::span<const RefPtr<Mesh>> meshes)
    {
        Meshes.assign(meshes.begin(), meshes.end());
    }

    IMPLEMENT_REFCOUNT(Model);
};