#pragma once

#include "BoundingVolumes.h"
#include "LevelDefs.h"
#include "shaders/ShaderInterop.h"

#include <Result.h>

struct StringResource final
{
    uint32_t Offset;
    uint32_t Length;
};

template<typename Tag>
struct TaggedStringResource final
{
    uint32_t Offset;
    uint32_t Length;
};

using TextureUriResource = TaggedStringResource<struct TextureUriTag>;

struct NodeNameResource final
{
    uint32_t NodeIndex;
    StringResource String;
};

struct MaterialResource final
{
    uint32_t BaseTextureIndex;
    RgbaColorf Color;
    float Metalness;
    float Roughness;
};

struct MeshResource final
{
    uint32_t IndexCount;
    uint32_t FirstIndex;
    uint32_t BaseVertex;
    uint32_t MaterialIndex;
    BoundingBox BoundingBox;
    BoundingSphere BoundingSphere;
};

struct ModelResource final
{
    uint32_t MeshOffset;
    uint32_t MeshCount;
    BoundingBox BoundingBox;
    BoundingSphere BoundingSphere;
};

struct MeshInstanceResource final
{
    uint32_t InstanceIndex;
    uint32_t MeshIndex;
};

struct ModelInstanceResource final
{
    uint32_t NodeIndex;
    uint32_t ModelIndex;
    uint32_t MeshInstanceOffset;
    uint32_t MeshInstanceCount;
};

struct ColliderResource final
{
    CollisionType CollisionType;
    ColliderShapeType ShapeType;

    struct SphereResource final
    {
        float Radius;
        Vec3f Center;
    };

    struct BoxResource final
    {
        Vec3f HalfExtents;
        Vec3f Center;
    };

    struct CapsuleResource final
    {
        float Radius;
        float HalfHeight;
        Vec3f Center;
    };

    union
    {
        SphereResource Sphere;
        BoxResource Box;
        CapsuleResource Capsule;
    };
};

struct RigidBodyResource final
{
    uint32_t NodeIndex;
    float Mass;
    MotionType MotionType;
    uint32_t ColliderOffset;
    uint32_t ColliderCount;
};

struct LevelNodeResource final
{
    uint32_t ParentIndex;
    uint32_t FirstChildIndex;
    uint32_t ChildCount;
    Vec3f LocalPos;
    Vec4f LocalRot;
    Vec3f LocalScale;
};

class ResourceBundle final
{
public:
    static constexpr uint64_t kInvalidOffset = std::numeric_limits<uint64_t>::max();
    static constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

    struct Header
    {
        uint64_t TotalSize{ kInvalidOffset };
        uint64_t CharsOffset{ kInvalidOffset };
        uint64_t NodeNamesOffset{ kInvalidOffset };
        uint64_t TextureUrisOffset{ kInvalidOffset };
        uint64_t MaterialsOffset{ kInvalidOffset };
        uint64_t VerticesOffset{ kInvalidOffset };
        uint64_t IndicesOffset{ kInvalidOffset };
        uint64_t MeshesOffset{ kInvalidOffset };
        uint64_t ModelsOffset{ kInvalidOffset };
        uint64_t MeshInstancesOffset{ kInvalidOffset };
        uint64_t ModelInstancesOffset{ kInvalidOffset };
        uint64_t DrawIndirectParamsOffset{ kInvalidOffset };
        uint64_t CollidersOffset{ kInvalidOffset };
        uint64_t RigidBodiesOffset{ kInvalidOffset };
        uint64_t NodesOffset{ kInvalidOffset };

        uint32_t CharsLength{ 0 };
        uint32_t NodeNameCount{ 0 };
        uint32_t TextureUriCount{ 0 };
        uint32_t MaterialCount{ 0 };
        uint32_t VertexCount{ 0 };
        uint32_t IndexCount{ 0 };
        uint32_t MeshCount{ 0 };
        uint32_t ModelCount{ 0 };
        uint32_t MeshInstanceCount{ 0 };
        uint32_t ModelInstanceCount{ 0 };
        uint32_t DrawIndirectParamsCount{ 0 };
        uint32_t ColliderCount{ 0 };
        uint32_t RigidBodyCount{ 0 };
        uint32_t NodeCount{ 0 };
    };

    ResourceBundle() = delete;

    explicit ResourceBundle(std::vector<char>&& buffer)
        : m_Buffer(std::move(buffer))
    {
        const void* p = m_Buffer.data();
        m_Header = static_cast<const Header*>(p);
    }

    std::span<const char> GetChars() const
    {
        return GetSpan<char>(m_Header->CharsOffset, m_Header->CharsLength);
    }

    std::span<const NodeNameResource> GetNodeNames() const
    {
        return GetSpan<NodeNameResource>(m_Header->NodeNamesOffset, m_Header->NodeNameCount);
    }

    std::span<const TextureUriResource> GetTextureUris() const
    {
        return GetSpan<TextureUriResource>(m_Header->TextureUrisOffset, m_Header->TextureUriCount);
    }

    std::span<const MaterialResource> GetMaterials() const
    {
        return GetSpan<MaterialResource>(m_Header->MaterialsOffset, m_Header->MaterialCount);
    }

    std::span<const Vertex> GetVertices() const
    {
        return GetSpan<Vertex>(m_Header->VerticesOffset, m_Header->VertexCount);
    }

    std::span<const VertexIndex> GetIndices() const
    {
        return GetSpan<VertexIndex>(m_Header->IndicesOffset, m_Header->IndexCount);
    }

    std::span<const MeshResource> GetMeshes() const
    {
        return GetSpan<MeshResource>(m_Header->MeshesOffset, m_Header->MeshCount);
    }

    std::span<const ModelResource> GetModels() const
    {
        return GetSpan<ModelResource>(m_Header->ModelsOffset, m_Header->ModelCount);
    }

    std::span<const MeshInstanceResource> GetMeshInstances() const
    {
        return GetSpan<MeshInstanceResource>(m_Header->MeshInstancesOffset,
            m_Header->MeshInstanceCount);
    }

    std::span<const ModelInstanceResource> GetModelInstances() const
    {
        return GetSpan<ModelInstanceResource>(m_Header->ModelInstancesOffset,
            m_Header->ModelInstanceCount);
    }

    std::span<const ShaderInterop::DrawIndirectParams> GetDrawIndirectParams() const
    {
        return GetSpan<ShaderInterop::DrawIndirectParams>(m_Header->DrawIndirectParamsOffset,
            m_Header->DrawIndirectParamsCount);
    }

    std::span<const ColliderResource> GetColliders() const
    {
        return GetSpan<ColliderResource>(m_Header->CollidersOffset, m_Header->ColliderCount);
    }

    std::span<const RigidBodyResource> GetRigidBodies() const
    {
        return GetSpan<RigidBodyResource>(m_Header->RigidBodiesOffset, m_Header->RigidBodyCount);
    }

    std::span<const LevelNodeResource> GetNodes() const
    {
        return GetSpan<LevelNodeResource>(m_Header->NodesOffset, m_Header->NodeCount);
    }

private:
    template<typename T>
    std::span<const T> GetSpan(const uint64_t offset, const uint32_t count) const
    {
        MLG_ASSERT(offset != kInvalidOffset, "Offset is invalid");
        MLG_ASSERT(count > 0, "Count is invalid");
        MLG_ASSERT(offset + (count * sizeof(T)) <= m_Header->TotalSize, "Span exceeds total size");

        const void* p = static_cast<const void*>(m_Buffer.data());
        const std::span s(static_cast<const char*>(p), m_Header->TotalSize);
        const void* p2 = s.subspan(offset).data();
        return std::span<const T>(static_cast<const T*>(p2), count);
    }

    const Header* m_Header;
    std::vector<char> m_Buffer;
};

class ResourceBundleBuilder final
{
public:
    Result<ResourceBundle> Build(const LevelDef& levelDef, const PropKitDef& propKitDef);

private:
    void AppendHeader(const uint64_t totalSize);
    void Append(const std::span<const char>& chars);
    void Append(const std::span<const NodeNameResource>& nodeNames);
    void Append(const std::span<const TextureUriResource>& textureUris);
    void Append(const std::span<const MaterialResource>& materials);
    void Append(const std::span<const Vertex>& vertices);
    void Append(const std::span<const VertexIndex>& indices);
    void Append(const std::span<const MeshResource>& meshes);
    void Append(const std::span<const ModelResource>& models);
    void Append(const std::span<const MeshInstanceResource>& meshInstances);
    void Append(const std::span<const ModelInstanceResource>& modelInstances);
    void Append(const std::span<const ShaderInterop::DrawIndirectParams>& drawIndirectParams);
    void Append(const std::span<const ColliderResource>& colliders);
    void Append(const std::span<const RigidBodyResource>& rigidBodies);
    void Append(const std::span<const LevelNodeResource>& nodes);

    ResourceBundle::Header* m_Header{ nullptr };
    std::vector<char> m_Buffer;
};