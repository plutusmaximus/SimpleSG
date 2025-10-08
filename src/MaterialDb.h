#pragma once

#include <vector>
#include <span>
#include <map>

#include "Material.h"

class MaterialDb
{
public:

    static RefPtr<MaterialDb> Create();

    void Add(RefPtr<Material> material);

    bool Contains(const MaterialId materialId) const;

    int GetIndex(const MaterialId materialId) const;

    RefPtr<Material> GetMaterial(const MaterialId materialId) const;

private:

    MaterialDb() = default;

    std::vector<RefPtr<Material>> m_Materials;
    std::map<MaterialId, int> m_MaterialIndexById;

    IMPLEMENT_REFCOUNT(MaterialDb);

    IMPLEMENT_NON_COPYABLE(MaterialDb);

};