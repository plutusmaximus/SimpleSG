#pragma once

#include "Bounds.h"
#include "SemanticIdentifier.h"
#include "StringArena.h"

using MaterialIdentifier = SemanticIdentifier<struct MaterialIdTag>;

class Mesh
{
public:
    Mesh() = delete;

    struct VertexParams
    {
        uint32_t IndexCount;
        uint32_t FirstIndex;
        uint32_t BaseVertex;
    };

    Mesh(const VertexParams& vertexParams,
        const MaterialIdentifier materialId,
        const BoundingBox& boundingBox,
        const BoundingSphere& boundingSphere)
        : m_IndexCount(vertexParams.IndexCount),
          m_FirstIndex(vertexParams.FirstIndex),
          m_BaseVertex(vertexParams.BaseVertex),
          m_MaterialId(materialId),
          m_BoundingBox(boundingBox),
          m_BoundingSphere(boundingSphere)
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
    Model(const StringHandle& name,
        const std::span<const Mesh>& meshes,
        const BoundingBox& boundingBox,
        const BoundingSphere& boundingSphere)
        : m_Name(name),
            m_Meshes(meshes),
            m_BoundingBox(boundingBox),
            m_BoundingSphere(boundingSphere)
    {
        MLG_ABORTIF(meshes.empty(), "Model must have at least one mesh");
    }

    const StringHandle& GetName() const { return m_Name; }
    std::span<const Mesh> GetMeshes() const { return m_Meshes; }
    const BoundingBox& GetBoundingBox() const { return m_BoundingBox; }
    const BoundingSphere& GetBoundingSphere() const { return m_BoundingSphere; }

private:
    StringHandle m_Name;
    std::span<const Mesh> m_Meshes;
    BoundingBox m_BoundingBox;
    BoundingSphere m_BoundingSphere;
};

class MeshInstance
{
public:

    MeshInstance() = delete;

    MeshInstance(const Mesh* mesh, const size_t instanceIndex)
        : m_Mesh(mesh), m_InstanceIndex(instanceIndex)
    {
        MLG_ABORTIF(!mesh, "MeshInstance cannot be created with an invalid mesh pointer");
    }

    const Mesh& GetMesh() const { return *m_Mesh; }
    MaterialIdentifier GetMaterialId() const { return m_Mesh->GetMaterialId(); }
    size_t GetInstanceIndex() const { return m_InstanceIndex; }
    const BoundingBox& GetBoundingBox() const { return m_Mesh->GetBoundingBox(); }
    const BoundingSphere& GetBoundingSphere() const { return m_Mesh->GetBoundingSphere(); }

private:

    const Mesh* m_Mesh{nullptr};
    size_t m_InstanceIndex{0};
};

class ModelInstance
{
public:

    ModelInstance() = delete;

    ModelInstance(const Model* model, const std::span<const MeshInstance>& meshInstances)
        : m_Model(model),
          m_MeshInstances(meshInstances)
    {
        MLG_ABORTIF(!model, "ModelInstance cannot be created with an invalid model pointer");
        MLG_ABORTIF(meshInstances.empty(), "ModelInstance cannot be created with an empty mesh instance list");
        MLG_ABORTIF(meshInstances.size() != model->GetMeshes().size(),
            "ModelInstance mesh instance count must match model mesh count");
    }

    const Model* GetModel() const { return m_Model; }
    const BoundingBox& GetBoundingBox() const { return m_Model->GetBoundingBox(); }
    const BoundingSphere& GetBoundingSphere() const { return m_Model->GetBoundingSphere(); }

    std::span<const MeshInstance> GetMeshInstances() const { return m_MeshInstances; }

    void SetVisible(const bool visible) { m_IsVisible = visible; }
    bool IsVisible() const { return m_IsVisible; }

private:
    const Model* m_Model{ nullptr };
    std::span<const MeshInstance> m_MeshInstances;

    bool m_IsVisible{ true };
};