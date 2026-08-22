#pragma once

#include "BoundingVolumes.h"
#include "SemanticIdentifier.h"

using MaterialIdentifier = SemanticIdentifier<struct MaterialIdTag>;

namespace wgpu
{
class BindGroup;
}

class Mesh
{
public:
    struct VertexParams
    {
        uint32_t IndexCount;
        uint32_t FirstIndex;
        uint32_t BaseVertex;
    };

    Mesh() = delete;

    Mesh(const VertexParams& vertexParams,
        const MaterialIdentifier materialId,
        const BoundingBox& boundingBox)
        : m_IndexCount(vertexParams.IndexCount),
          m_FirstIndex(vertexParams.FirstIndex),
          m_BaseVertex(vertexParams.BaseVertex),
          m_MaterialId(materialId),
          m_BoundingBox(boundingBox),
          m_BoundingSphere(boundingBox)
    {
    }

    uint32_t GetIndexCount() const { return m_IndexCount; }
    uint32_t GetFirstIndex() const { return m_FirstIndex; }
    uint32_t GetBaseVertex() const { return m_BaseVertex; }
    MaterialIdentifier GetMaterialId() const { return m_MaterialId; }
    const BoundingBox& GetBoundingBox() const { return m_BoundingBox; }
    const BoundingSphere& GetBoundingSphere() const { return m_BoundingSphere; }

private:
    uint32_t m_IndexCount;
    uint32_t m_FirstIndex;
    uint32_t m_BaseVertex;
    MaterialIdentifier m_MaterialId;
    BoundingBox m_BoundingBox;
    BoundingSphere m_BoundingSphere;
};

class Model
{
public:
    Model() = delete;

    Model(const std::span<const Mesh>& meshes,
        const BoundingBox& boundingBox)
        : m_Meshes(meshes),
          m_BoundingBox(boundingBox),
          m_BoundingSphere(boundingBox)
    {
        MLG_ABORTIF(meshes.empty(), "Model must have at least one mesh");
    }

    std::span<const Mesh> GetMeshes() const { return m_Meshes; }
    const BoundingBox& GetBoundingBox() const { return m_BoundingBox; }
    const BoundingSphere& GetBoundingSphere() const { return m_BoundingSphere; }

private:
    std::span<const Mesh> m_Meshes;
    BoundingBox m_BoundingBox;
    BoundingSphere m_BoundingSphere;
};

class MeshInstance
{
public:
    MeshInstance() = delete;

    MeshInstance(const Mesh* mesh, const size_t instanceIndex)
        : m_Mesh(mesh),
          m_InstanceIndex(instanceIndex)
    {
        MLG_ABORTIF(!mesh, "MeshInstance cannot be created with an invalid mesh pointer");
    }

    MaterialIdentifier GetMaterialId() const { return m_Mesh->GetMaterialId(); }
    uint32_t GetIndexCount() const { return m_Mesh->GetIndexCount(); }
    uint32_t GetFirstIndex() const { return m_Mesh->GetFirstIndex(); }
    uint32_t GetBaseVertex() const { return m_Mesh->GetBaseVertex(); }
    const BoundingBox& GetBoundingBox() const { return m_Mesh->GetBoundingBox(); }
    const BoundingSphere& GetBoundingSphere() const { return m_Mesh->GetBoundingSphere(); }
    size_t GetInstanceIndex() const { return m_InstanceIndex; }

private:
    const Mesh* m_Mesh{ nullptr };
    size_t m_InstanceIndex{ 0 };
};