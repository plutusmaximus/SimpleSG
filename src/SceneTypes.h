#pragma once

#include "Bounds.h"
#include "SemanticIdentifier.h"
#include "StringArena.h"

using MeshIdentifier = SemanticIdentifier<struct MeshIdTag>;
using MaterialIdentifier = SemanticIdentifier<struct MaterialIdTag>;
using ModelIdentifier = SemanticIdentifier<struct ModelIdTag>;

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
        const MeshIdentifier& firstMeshId,
        const size_t meshCount,
        const BoundingBox& boundingBox,
        const BoundingSphere& boundingSphere)
        : m_Name(name),
            m_FirstMeshId(firstMeshId),
            m_MeshCount(meshCount),
            m_BoundingBox(boundingBox),
            m_BoundingSphere(boundingSphere)
    {
    }

    const StringHandle& GetName() const { return m_Name; }
    const MeshIdentifier& GetFirstMeshId() const { return m_FirstMeshId; }
    size_t GetMeshCount() const { return m_MeshCount; }
    const BoundingBox& GetBoundingBox() const { return m_BoundingBox; }
    const BoundingSphere& GetBoundingSphere() const { return m_BoundingSphere; }

private:
    StringHandle m_Name;
    MeshIdentifier m_FirstMeshId;
    size_t m_MeshCount{ 0 };
    BoundingBox m_BoundingBox;
    BoundingSphere m_BoundingSphere;
};

class ModelInstance
{
public:

    ModelInstance() = delete;

    explicit ModelInstance(const ModelIdentifier modelId)
        : m_ModelId(modelId)
    {
        MLG_ASSERT(modelId.IsValid(), "ModelInstance cannot be created with invalid ModelIdentifier");
    }

    ModelIdentifier GetModelId() const { return m_ModelId; }

    void SetVisible(const bool visible) { m_IsVisible = visible; }
    bool IsVisible() const { return m_IsVisible; }

private:
    ModelIdentifier m_ModelId;

    bool m_IsVisible{ true };
};

class MeshInstance
{
public:

    MeshInstance() = delete;

    MeshInstance(const Mesh* mesh, const size_t instanceIndex)
        : m_Mesh(mesh), m_InstanceIndex(instanceIndex)
    {
    }

    const Mesh& GetMesh() const { return *m_Mesh; }
    size_t GetInstanceIndex() const { return m_InstanceIndex; }

private:

    const Mesh* m_Mesh{nullptr};
    size_t m_InstanceIndex{0};
};