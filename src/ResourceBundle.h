#pragma once

#include "BoundingVolumes.h"
#include "LevelDefs.h"
#include "Result.h"

#include <bit>

/**
Format contract:
Byte order:          little-endian
Integers:            fixed-width std::uintN_t / std::intN_t
Floats:              IEEE-754 binary32/binary64
Pointers:            prohibited
size_t/ptrdiff_t:    prohibited
bool:                prohibited in persistent structs
Enums:               explicit underlying type
References:          uint32 offsets or indices
Object placement:    explicitly aligned
Struct requirements: standard-layout + trivially-copyable
Layout:              sizeof/offsetof compile-time verified
*/

struct Resource
{
    static constexpr uint64_t kInvalidOffset = std::numeric_limits<uint64_t>::max();
    static constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();
};

static_assert(sizeof(std::uint32_t) == 4);
static_assert(sizeof(std::uint64_t) == 8); // NOLINT(readability-magic-numbers)
static_assert(sizeof(float) == 4);
static_assert(std::numeric_limits<float>::is_iec559);
static_assert(sizeof(std::int32_t) == 4);
static_assert(sizeof(std::int64_t) == 8); // NOLINT(readability-magic-numbers)
static_assert(std::endian::native == std::endian::little);
static_assert(std::is_unsigned_v<VertexIndex>);
static_assert(std::numeric_limits<VertexIndex>::max()
    >= std::numeric_limits<uint32_t>::max()); // NOLINT(misc-redundant-expression)

/// Concept for types that can be safely read from and written to binary streams.
template<class T>
concept BinaryStruct = std::is_standard_layout_v<T> && std::is_trivially_copyable_v<T>;

// Compile-time assertions for struct offsets and sizes that will print
// a compiler error that includes the actual offset or size.

template<std::size_t Expected, std::size_t Actual>
struct mlg_assert_offset
{
    static_assert(Actual == Expected, "offset mismatch");
};

template<typename T, std::size_t Expected, std::size_t Actual = sizeof(T)>
struct mlg_assert_size
{
    static_assert(Actual == Expected, "size mismatch");
};

#define MLG_CONCAT2(a, b) a##b
#define MLG_CONCAT(a, b) MLG_CONCAT2(a, b)

/// Compile-time assertions for struct offsets and sizes.
#define MLG_ASSERT_OFFSET(struct_name, member, offset)                                             \
    mlg_assert_offset<offset, offsetof(struct_name, member)> static const MLG_CONCAT(              \
        __assert_offset_,                                                                          \
        __LINE__);

#define MLG_ASSERT_SIZE(struct_name, size)                                                         \
    mlg_assert_size<struct_name, size> static const MLG_CONCAT(__assert_size_, __LINE__);

// NOLINTBEGIN(bugprone-macro-parentheses)
#define MLG_COUNT_FIELD(type, name, ...) +1
#define MLG_SIZE_FIELD(type, name, ...) +sizeof(type)
// NOLINTEND(bugprone-macro-parentheses)

#define MLG_DECLARE_FIELD(type, name, ...) type name __VA_OPT__(= __VA_ARGS__);

#define MLG_FIELD_COUNT(fields) (0 fields(MLG_COUNT_FIELD))

#define MLG_FIELD_SIZE_SUM(fields) (0 fields(MLG_SIZE_FIELD))

#define MLG_ASSERT_NO_PADDING(type, fields)                                                        \
    static_assert(sizeof(type) == MLG_FIELD_SIZE_SUM(fields))

#define MLG_ASSERT_FIELD_COUNT(fields, count) static_assert(MLG_FIELD_COUNT(fields) == (count))

#define STRING_RESOURCE_FIELDS(X)                                                                  \
    X(uint32_t, Offset, 0)                                                                         \
    X(uint32_t, Length, 0)

/// StringResource

struct StringResource final
{
    STRING_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<StringResource>);
MLG_ASSERT_FIELD_COUNT(STRING_RESOURCE_FIELDS, 2);
MLG_ASSERT_NO_PADDING(StringResource, STRING_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(StringResource, Offset, 0)
MLG_ASSERT_OFFSET(StringResource, Length, 4)
MLG_ASSERT_SIZE(StringResource, 8)

/// NodeNameResource

#define NODE_NAME_RESOURCE_FIELDS(X)                                                               \
    X(uint32_t, NodeIndex, Resource::kInvalidIndex)                                                \
    X(StringResource, String)

struct NodeNameResource final
{
    NODE_NAME_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<NodeNameResource>);
MLG_ASSERT_FIELD_COUNT(NODE_NAME_RESOURCE_FIELDS, 2);
MLG_ASSERT_NO_PADDING(NodeNameResource, NODE_NAME_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(NodeNameResource, NodeIndex, 0)
MLG_ASSERT_OFFSET(NodeNameResource, String, 4)
MLG_ASSERT_SIZE(NodeNameResource, 12)

/// MaterialResource

#define MATERIAL_RESOURCE_FIELDS(X)                                                                \
    X(uint32_t, BaseTextureIndex, )                                                                \
    X(RgbaColorf, Color, { 1, 0, 1, 1 })                                                           \
    X(float, Metalness, 0)                                                                         \
    X(float, Roughness, 0)

struct MaterialResource final
{
    MATERIAL_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<MaterialResource>);
MLG_ASSERT_FIELD_COUNT(MATERIAL_RESOURCE_FIELDS, 4);
MLG_ASSERT_NO_PADDING(MaterialResource, MATERIAL_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(MaterialResource, BaseTextureIndex, 0)
MLG_ASSERT_OFFSET(MaterialResource, Color, 4)
MLG_ASSERT_OFFSET(MaterialResource, Metalness, 20)
MLG_ASSERT_OFFSET(MaterialResource, Roughness, 24)
MLG_ASSERT_SIZE(MaterialResource, 28)

/// MeshResource

#define MESH_RESOURCE_FIELDS(X)                                                                    \
    X(uint32_t, IndexCount, 0)                                                                     \
    X(uint32_t, FirstIndex, 0)                                                                     \
    X(uint32_t, BaseVertex, 0)                                                                     \
    X(uint32_t, MaterialIndex, Resource::kInvalidIndex)                                            \
    X(BoundingBox, BoundingBox)                                                                    \
    X(BoundingSphere, BoundingSphere)

struct MeshResource final
{
    MESH_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<MeshResource>);
MLG_ASSERT_FIELD_COUNT(MESH_RESOURCE_FIELDS, 6);
MLG_ASSERT_NO_PADDING(MeshResource, MESH_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(MeshResource, IndexCount, 0)
MLG_ASSERT_OFFSET(MeshResource, FirstIndex, 4)
MLG_ASSERT_OFFSET(MeshResource, BaseVertex, 8)
MLG_ASSERT_OFFSET(MeshResource, MaterialIndex, 12)
MLG_ASSERT_OFFSET(MeshResource, BoundingBox, 16)
MLG_ASSERT_OFFSET(MeshResource, BoundingSphere, 40)
MLG_ASSERT_SIZE(MeshResource, 56)

/// ModelResource

#define MODEL_RESOURCE_FIELDS(X)                                                                   \
    X(uint32_t, MeshOffset, 0)                                                                     \
    X(uint32_t, MeshCount, 0)                                                                      \
    X(BoundingBox, BoundingBox)                                                                    \
    X(BoundingSphere, BoundingSphere)

struct ModelResource final
{
    MODEL_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<ModelResource>);
MLG_ASSERT_FIELD_COUNT(MODEL_RESOURCE_FIELDS, 4);
MLG_ASSERT_NO_PADDING(ModelResource, MODEL_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ModelResource, MeshOffset, 0)
MLG_ASSERT_OFFSET(ModelResource, MeshCount, 4)
MLG_ASSERT_OFFSET(ModelResource, BoundingBox, 8)
MLG_ASSERT_OFFSET(ModelResource, BoundingSphere, 32)
MLG_ASSERT_SIZE(ModelResource, 48)

/// ModelInstanceResource

#define MODEL_INSTANCE_RESOURCE_FIELDS(X)                                                          \
    X(uint32_t, NodeIndex, Resource::kInvalidIndex)                                                \
    X(uint32_t, ModelIndex, Resource::kInvalidIndex)                                               \
    X(uint32_t, MeshInstanceOffset, 0)

struct ModelInstanceResource final
{
    MODEL_INSTANCE_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<ModelInstanceResource>);
MLG_ASSERT_FIELD_COUNT(MODEL_INSTANCE_RESOURCE_FIELDS, 3);
MLG_ASSERT_NO_PADDING(ModelInstanceResource, MODEL_INSTANCE_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ModelInstanceResource, NodeIndex, 0)
MLG_ASSERT_OFFSET(ModelInstanceResource, ModelIndex, 4)
MLG_ASSERT_OFFSET(ModelInstanceResource, MeshInstanceOffset, 8)
MLG_ASSERT_SIZE(ModelInstanceResource, 12)

/// ColliderResource

#define SPHERE_RESOURCE_FIELDS(X)                                                                  \
    X(float, Radius)                                                                               \
    X(Vec3f, Center)

#define BOX_RESOURCE_FIELDS(X)                                                                     \
    X(Vec3f, HalfExtents)                                                                          \
    X(Vec3f, Center)

#define CAPSULE_RESOURCE_FIELDS(X)                                                                 \
    X(float, Radius)                                                                               \
    X(float, HalfHeight)                                                                           \
    X(Vec3f, Center)

#define COLLIDER_RESOURCE_FIELDS(X)                                                                \
    X(CollisionType, CollisionType)                                                                \
    X(ColliderShapeType, ShapeType)                                                                \
    X(ColliderResource::Shape, Shape)

struct ColliderResource final
{
    struct Sphere final
    {
        SPHERE_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
    };

    struct Box final
    {
        BOX_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
    };

    struct Capsule final
    {
        CAPSULE_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
    };

    union Shape
    {
        Shape() = delete;
        Shape(const Sphere& sphere) // NOLINT(google-explicit-constructor)
            : Sphere(sphere)
        {
        }
        Shape(const Box& box) // NOLINT(google-explicit-constructor)
            : Box(box)
        {
        }
        Shape(const Capsule& capsule) // NOLINT(google-explicit-constructor)
            : Capsule(capsule)
        {
        }

        Sphere Sphere;
        Box Box;
        Capsule Capsule;
    };

    COLLIDER_RESOURCE_FIELDS(MLG_DECLARE_FIELD)

    const Sphere& GetSphere() const
    {
        MLG_ASSERT(ShapeType == ColliderShapeType::Sphere);
        return Shape.Sphere; // NOLINT(cppcoreguidelines-pro-type-union-access)
    }

    const Box& GetBox() const
    {
        MLG_ASSERT(ShapeType == ColliderShapeType::Box);
        return Shape.Box; // NOLINT(cppcoreguidelines-pro-type-union-access)
    }

    const Capsule& GetCapsule() const
    {
        MLG_ASSERT(ShapeType == ColliderShapeType::Capsule);
        return Shape.Capsule; // NOLINT(cppcoreguidelines-pro-type-union-access)
    }
};
static_assert(BinaryStruct<ColliderResource::Sphere>);
MLG_ASSERT_FIELD_COUNT(SPHERE_RESOURCE_FIELDS, 2);
MLG_ASSERT_NO_PADDING(ColliderResource::Sphere, SPHERE_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ColliderResource::Sphere, Radius, 0)
MLG_ASSERT_OFFSET(ColliderResource::Sphere, Center, 4)
MLG_ASSERT_SIZE(ColliderResource::Sphere, 16)

static_assert(BinaryStruct<ColliderResource::Box>);
MLG_ASSERT_FIELD_COUNT(BOX_RESOURCE_FIELDS, 2);
MLG_ASSERT_NO_PADDING(ColliderResource::Box, BOX_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ColliderResource::Box, HalfExtents, 0)
MLG_ASSERT_OFFSET(ColliderResource::Box, Center, 12)
MLG_ASSERT_SIZE(ColliderResource::Box, 24)

static_assert(BinaryStruct<ColliderResource::Capsule>);
MLG_ASSERT_FIELD_COUNT(CAPSULE_RESOURCE_FIELDS, 3);
MLG_ASSERT_NO_PADDING(ColliderResource::Capsule, CAPSULE_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ColliderResource::Capsule, Radius, 0)
MLG_ASSERT_OFFSET(ColliderResource::Capsule, HalfHeight, 4)
MLG_ASSERT_OFFSET(ColliderResource::Capsule, Center, 8)
MLG_ASSERT_SIZE(ColliderResource::Capsule, 20)

static_assert(BinaryStruct<ColliderResource>);
MLG_ASSERT_FIELD_COUNT(COLLIDER_RESOURCE_FIELDS, 3);
MLG_ASSERT_NO_PADDING(ColliderResource, COLLIDER_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ColliderResource, CollisionType, 0)
MLG_ASSERT_OFFSET(ColliderResource, ShapeType, 4)
MLG_ASSERT_OFFSET(ColliderResource, Shape, 8)
MLG_ASSERT_SIZE(ColliderResource, 32)

/// RigidBodyResource

#define RIGID_BODY_RESOURCE_FIELDS(X)                                                              \
    X(uint32_t, NodeIndex, Resource::kInvalidIndex)                                                \
    X(float, Mass, 0.0f)                                                                           \
    X(MotionType, MotionType)                                                                      \
    X(uint32_t, ColliderOffset, 0)                                                                 \
    X(uint32_t, ColliderCount, 0)

struct RigidBodyResource final
{
    RIGID_BODY_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<RigidBodyResource>);
MLG_ASSERT_FIELD_COUNT(RIGID_BODY_RESOURCE_FIELDS, 5);
MLG_ASSERT_NO_PADDING(RigidBodyResource, RIGID_BODY_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(RigidBodyResource, NodeIndex, 0)
MLG_ASSERT_OFFSET(RigidBodyResource, Mass, 4)
MLG_ASSERT_OFFSET(RigidBodyResource, MotionType, 8)
MLG_ASSERT_OFFSET(RigidBodyResource, ColliderOffset, 12)
MLG_ASSERT_OFFSET(RigidBodyResource, ColliderCount, 16)
MLG_ASSERT_SIZE(RigidBodyResource, 20)

/// LevelNodeResource

#define LEVEL_NODE_RESOURCE_FIELDS(X)                                                              \
    X(uint32_t, ParentIndex, Resource::kInvalidIndex)                                              \
    X(uint32_t, FirstChildIndex, Resource::kInvalidIndex)                                          \
    X(uint32_t, ChildCount, 0)                                                                     \
    X(Vec3f, LocalPos, Vec3f{ 0.0f, 0.0f, 0.0f })                                                  \
    X(Vec4f, LocalRot, Vec4f{ 0.0f, 0.0f, 0.0f, 1.0f })                                            \
    X(Vec3f, LocalScale, Vec3f{ 1.0f, 1.0f, 1.0f })

struct LevelNodeResource final
{
    LEVEL_NODE_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<LevelNodeResource>);
MLG_ASSERT_FIELD_COUNT(LEVEL_NODE_RESOURCE_FIELDS, 6);
MLG_ASSERT_NO_PADDING(LevelNodeResource, LEVEL_NODE_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(LevelNodeResource, ParentIndex, 0)
MLG_ASSERT_OFFSET(LevelNodeResource, FirstChildIndex, 4)
MLG_ASSERT_OFFSET(LevelNodeResource, ChildCount, 8)
MLG_ASSERT_OFFSET(LevelNodeResource, LocalPos, 12)
MLG_ASSERT_OFFSET(LevelNodeResource, LocalRot, 24)
MLG_ASSERT_OFFSET(LevelNodeResource, LocalScale, 40)
MLG_ASSERT_SIZE(LevelNodeResource, 52)

class ResourceBundle final
{
public:
#define RESOURCE_BUNDLE_HEADER_FIELDS(X)                                                           \
    X(uint64_t, TotalSize, 0)                                                                      \
    X(uint64_t, CharsOffset, Resource::kInvalidOffset)                                             \
    X(uint64_t, NodeNamesOffset, Resource::kInvalidOffset)                                         \
    X(uint64_t, TextureUrisOffset, Resource::kInvalidOffset)                                       \
    X(uint64_t, MaterialsOffset, Resource::kInvalidOffset)                                         \
    X(uint64_t, VerticesOffset, Resource::kInvalidOffset)                                          \
    X(uint64_t, IndicesOffset, Resource::kInvalidOffset)                                           \
    X(uint64_t, MeshesOffset, Resource::kInvalidOffset)                                            \
    X(uint64_t, ModelsOffset, Resource::kInvalidOffset)                                            \
    X(uint64_t, ModelInstancesOffset, Resource::kInvalidOffset)                                    \
    X(uint64_t, CollidersOffset, Resource::kInvalidOffset)                                         \
    X(uint64_t, RigidBodiesOffset, Resource::kInvalidOffset)                                       \
    X(uint64_t, NodesOffset, Resource::kInvalidOffset)                                             \
    X(uint32_t, CharsLength, 0)                                                                    \
    X(uint32_t, NodeNameCount, 0)                                                                  \
    X(uint32_t, TextureUriCount, 0)                                                                \
    X(uint32_t, MaterialCount, 0)                                                                  \
    X(uint32_t, VertexCount, 0)                                                                    \
    X(uint32_t, IndexCount, 0)                                                                     \
    X(uint32_t, MeshCount, 0)                                                                      \
    X(uint32_t, ModelCount, 0)                                                                     \
    X(uint32_t, ModelInstanceCount, 0)                                                             \
    X(uint32_t, ColliderCount, 0)                                                                  \
    X(uint32_t, RigidBodyCount, 0)                                                                 \
    X(uint32_t, NodeCount, 0)    

    struct Header
    {
        RESOURCE_BUNDLE_HEADER_FIELDS(MLG_DECLARE_FIELD)
    };
    static_assert(BinaryStruct<Header>);
    MLG_ASSERT_FIELD_COUNT(RESOURCE_BUNDLE_HEADER_FIELDS, 25);
    MLG_ASSERT_NO_PADDING(Header, RESOURCE_BUNDLE_HEADER_FIELDS);
    MLG_ASSERT_OFFSET(Header, TotalSize, 0)
    MLG_ASSERT_OFFSET(Header, CharsOffset, 8)
    MLG_ASSERT_OFFSET(Header, NodeNamesOffset, 16)
    MLG_ASSERT_OFFSET(Header, TextureUrisOffset, 24)
    MLG_ASSERT_OFFSET(Header, MaterialsOffset, 32)
    MLG_ASSERT_OFFSET(Header, VerticesOffset, 40)
    MLG_ASSERT_OFFSET(Header, IndicesOffset, 48)
    MLG_ASSERT_OFFSET(Header, MeshesOffset, 56)
    MLG_ASSERT_OFFSET(Header, ModelsOffset, 64)
    MLG_ASSERT_OFFSET(Header, ModelInstancesOffset, 72)
    MLG_ASSERT_OFFSET(Header, CollidersOffset, 80)
    MLG_ASSERT_OFFSET(Header, RigidBodiesOffset, 88)
    MLG_ASSERT_OFFSET(Header, NodesOffset, 96)
    MLG_ASSERT_OFFSET(Header, CharsLength, 104)
    MLG_ASSERT_OFFSET(Header, NodeNameCount, 108)
    MLG_ASSERT_OFFSET(Header, TextureUriCount, 112)
    MLG_ASSERT_OFFSET(Header, MaterialCount, 116)
    MLG_ASSERT_OFFSET(Header, VertexCount, 120)
    MLG_ASSERT_OFFSET(Header, IndexCount, 124)
    MLG_ASSERT_OFFSET(Header, MeshCount, 128)
    MLG_ASSERT_OFFSET(Header, ModelCount, 132)
    MLG_ASSERT_OFFSET(Header, ModelInstanceCount, 136)
    MLG_ASSERT_OFFSET(Header, ColliderCount, 140)
    MLG_ASSERT_OFFSET(Header, RigidBodyCount, 144)
    MLG_ASSERT_OFFSET(Header, NodeCount, 148)
    MLG_ASSERT_SIZE(Header, 152)

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

    std::span<const StringResource> GetTextureUris() const
    {
        return GetSpan<StringResource>(m_Header->TextureUrisOffset, m_Header->TextureUriCount);
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

    std::span<const ModelInstanceResource> GetModelInstances() const
    {
        return GetSpan<ModelInstanceResource>(m_Header->ModelInstancesOffset,
            m_Header->ModelInstanceCount);
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
        MLG_ASSERT(offset != Resource::kInvalidOffset, "Offset is invalid");
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
    void Append(const std::span<const StringResource>& textureUris);
    void Append(const std::span<const MaterialResource>& materials);
    void Append(const std::span<const Vertex>& vertices);
    void Append(const std::span<const VertexIndex>& indices);
    void Append(const std::span<const MeshResource>& meshes);
    void Append(const std::span<const ModelResource>& models);
    void Append(const std::span<const ModelInstanceResource>& modelInstances);
    void Append(const std::span<const ColliderResource>& colliders);
    void Append(const std::span<const RigidBodyResource>& rigidBodies);
    void Append(const std::span<const LevelNodeResource>& nodes);

    ResourceBundle::Header* m_Header{ nullptr };
    std::vector<char> m_Buffer;
};
