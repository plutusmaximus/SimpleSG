#pragma once

#include "Mesh.h"

#include <vector>

class Model
{
public:
    template<int COUNT>
    static RefPtr<Model> Create(const RefPtr<Mesh> (&meshes)[COUNT])
    {
        return Create(meshes, COUNT);
    }

    static RefPtr<Model> Create(const RefPtr<Mesh>* meshes, const int meshCount)
    {
        return new Model(meshes, meshCount);
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

    Meshes Meshes;

private:

    Model() = delete;

    Model(const RefPtr<Mesh>* meshes, const int meshCount)
    {
        Meshes.insert(Meshes.end(), &meshes[0], &meshes[meshCount]);
    }

    IMPLEMENT_NON_COPYABLE(Model);

    IMPLEMENT_REFCOUNT(Model);
};