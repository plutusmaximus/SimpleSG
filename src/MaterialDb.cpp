#include "MaterialDb.h"

RefPtr<MaterialDb>
MaterialDb::Create()
{
    return new MaterialDb();
}

void
MaterialDb::Add(RefPtr<Material> material)
{
    const int count = static_cast<int>(std::size(m_Materials));
    m_Materials.push_back(material);

    m_MaterialIndexById[material->Id] = count;
}

bool
MaterialDb::Contains(const MaterialId materialId) const
{
    return m_MaterialIndexById.contains(materialId);
}

int
MaterialDb::GetIndex(const MaterialId materialId) const
{
    return Contains(materialId) ? m_MaterialIndexById.find(materialId)->second : -1;
}

RefPtr<Material>
MaterialDb::GetMaterial(const MaterialId materialId) const
{
    const int idx = GetIndex(materialId);

    return (idx >= 0) ? m_Materials[idx] : RefPtr<Material>();
}