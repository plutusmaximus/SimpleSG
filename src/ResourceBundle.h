#pragma once

#include "AssertHelper.h"
#include "BoundingVolumes.h"
#include "Color.h"
#include "PhysicsTypes.h"
#include "Result.h"

#include <bit>
#include <string_view>
#include <vector>

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

struct LevelDef;
struct PropKitDef;

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

template<size_t Expected, size_t Actual>
struct mlg_assert_offset
{
    static_assert(Actual == Expected, "offset mismatch");
};

template<typename T, size_t Expected, size_t Actual = sizeof(T)>
struct mlg_assert_size
{
    static_assert(Actual == Expected, "size mismatch");
};

#define MLG_ASSERT_OFFSET(struct_name, member, offset)                                             \
    static_assert(offsetof(struct_name, member) == (offset), "offset mismatch");

#define MLG_ASSERT_SIZE(struct_name, size)                                                         \
    static_assert(sizeof(struct_name) == (size), "size mismatch");

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

struct StringResource;
struct NodeNameResource;
struct MaterialResource;
struct MeshResource;
struct ModelResource;
struct ModelInstanceResource;
struct ColliderResource;
struct RigidBodyResource;
struct LevelNodeResource;

class ResourceBundle final
{
public:
    using OffsetType = uint32_t;
    using IndexType = uint32_t;
    using CountType = uint32_t;

    static constexpr OffsetType kInvalidOffset = std::numeric_limits<OffsetType>::max();
    static constexpr IndexType kInvalidIndex = std::numeric_limits<IndexType>::max();

    // Maximum allowed size for a resource bundle.
    // This is currently set to the maximum value of a 32-bit unsigned integer
    // in order to maximize browser compatibility.
    static constexpr OffsetType kMaxBundleSize = std::numeric_limits<OffsetType>::max();

    static constexpr OffsetType kMaxOffset = kMaxBundleSize - 1;
    static constexpr IndexType kMaxIndex = std::numeric_limits<IndexType>::max() - 1;
    static constexpr CountType kMaxCount = std::numeric_limits<CountType>::max();

#define RESOURCE_BUNDLE_HEADER_FIELDS(X)                                                           \
    X(OffsetType, TotalSize, 0)                                                                    \
    X(OffsetType, CharsOffset, kInvalidOffset)                                                     \
    X(OffsetType, NodeNamesOffset, kInvalidOffset)                                                 \
    X(OffsetType, TextureUrisOffset, kInvalidOffset)                                               \
    X(OffsetType, MaterialsOffset, kInvalidOffset)                                                 \
    X(OffsetType, VerticesOffset, kInvalidOffset)                                                  \
    X(OffsetType, IndicesOffset, kInvalidOffset)                                                   \
    X(OffsetType, MeshesOffset, kInvalidOffset)                                                    \
    X(OffsetType, ModelsOffset, kInvalidOffset)                                                    \
    X(OffsetType, ModelInstancesOffset, kInvalidOffset)                                            \
    X(OffsetType, CollidersOffset, kInvalidOffset)                                                 \
    X(OffsetType, RigidBodiesOffset, kInvalidOffset)                                               \
    X(OffsetType, NodesOffset, kInvalidOffset)                                                     \
    X(CountType, CharsLength, 0)                                                                   \
    X(CountType, NodeNameCount, 0)                                                                 \
    X(CountType, TextureUriCount, 0)                                                               \
    X(CountType, MaterialCount, 0)                                                                 \
    X(CountType, VertexCount, 0)                                                                   \
    X(CountType, IndexCount, 0)                                                                    \
    X(CountType, MeshCount, 0)                                                                     \
    X(CountType, ModelCount, 0)                                                                    \
    X(CountType, ModelInstanceCount, 0)                                                            \
    X(CountType, ColliderCount, 0)                                                                 \
    X(CountType, RigidBodyCount, 0)                                                                \
    X(CountType, NodeCount, 0)

    struct Header
    {
        RESOURCE_BUNDLE_HEADER_FIELDS(MLG_DECLARE_FIELD)
    };
    static_assert(BinaryStruct<Header>);
    MLG_ASSERT_FIELD_COUNT(RESOURCE_BUNDLE_HEADER_FIELDS, 25);
    MLG_ASSERT_NO_PADDING(Header, RESOURCE_BUNDLE_HEADER_FIELDS);
    MLG_ASSERT_OFFSET(Header, TotalSize, 0)
    MLG_ASSERT_OFFSET(Header, CharsOffset, 4)
    MLG_ASSERT_OFFSET(Header, NodeNamesOffset, 8)
    MLG_ASSERT_OFFSET(Header, TextureUrisOffset, 12)
    MLG_ASSERT_OFFSET(Header, MaterialsOffset, 16)
    MLG_ASSERT_OFFSET(Header, VerticesOffset, 20)
    MLG_ASSERT_OFFSET(Header, IndicesOffset, 24)
    MLG_ASSERT_OFFSET(Header, MeshesOffset, 28)
    MLG_ASSERT_OFFSET(Header, ModelsOffset, 32)
    MLG_ASSERT_OFFSET(Header, ModelInstancesOffset, 36)
    MLG_ASSERT_OFFSET(Header, CollidersOffset, 40)
    MLG_ASSERT_OFFSET(Header, RigidBodiesOffset, 44)
    MLG_ASSERT_OFFSET(Header, NodesOffset, 48)
    MLG_ASSERT_OFFSET(Header, CharsLength, 52)
    MLG_ASSERT_OFFSET(Header, NodeNameCount, 56)
    MLG_ASSERT_OFFSET(Header, TextureUriCount, 60)
    MLG_ASSERT_OFFSET(Header, MaterialCount, 64)
    MLG_ASSERT_OFFSET(Header, VertexCount, 68)
    MLG_ASSERT_OFFSET(Header, IndexCount, 72)
    MLG_ASSERT_OFFSET(Header, MeshCount, 76)
    MLG_ASSERT_OFFSET(Header, ModelCount, 80)
    MLG_ASSERT_OFFSET(Header, ModelInstanceCount, 84)
    MLG_ASSERT_OFFSET(Header, ColliderCount, 88)
    MLG_ASSERT_OFFSET(Header, RigidBodyCount, 92)
    MLG_ASSERT_OFFSET(Header, NodeCount, 96)
    MLG_ASSERT_SIZE(Header, 100)

    ResourceBundle() = delete;

    explicit ResourceBundle(std::vector<char>&& buffer)
        : m_Buffer(std::move(buffer))
    {
        const void* p = m_Buffer.data();
        m_Header = static_cast<const Header*>(p);
    }

    /// Clears the resource bundle, releasing its internal buffer and resetting the header pointer.
    void Clear()
    {
        m_Buffer.clear();
        const std::vector<char> bye = std::move(m_Buffer);
        m_Header = nullptr;
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

    std::string_view GetStringView(const StringResource& stringResource) const;

private:
    template<typename T>
    std::span<const T> GetSpan(const OffsetType offset, const CountType count) const
    {
        MLG_ASSERT(offset != kInvalidOffset, "Offset is invalid");

        const std::span s(m_Buffer);
        MLG_ABORTIF(offset > s.size() || count > (s.size() - offset) / sizeof(T),
            "Span exceeds total size");
        const void* p2 = s.subspan(static_cast<size_t>(offset)).data();
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
    void AppendHeader(const ResourceBundle::OffsetType totalSize);
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

/// StringResource

#define STRING_RESOURCE_FIELDS(X)                                                                  \
    X(ResourceBundle::IndexType, CharIndex, 0)                                                     \
    X(ResourceBundle::CountType, Length, 0)

struct StringResource final
{
    STRING_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<StringResource>);
MLG_ASSERT_FIELD_COUNT(STRING_RESOURCE_FIELDS, 2);
MLG_ASSERT_NO_PADDING(StringResource, STRING_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(StringResource, CharIndex, 0)
MLG_ASSERT_OFFSET(StringResource, Length, 4)
MLG_ASSERT_SIZE(StringResource, 8)

/// NodeNameResource

#define NODE_NAME_RESOURCE_FIELDS(X)                                                               \
    X(ResourceBundle::IndexType, NodeIndex, ResourceBundle::kInvalidIndex)                         \
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
    X(ResourceBundle::IndexType, BaseTextureIndex, ResourceBundle::kInvalidIndex)                  \
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
    X(ResourceBundle::CountType, IndexCount, 0)                                                    \
    X(ResourceBundle::IndexType, FirstIndex, 0)                                                    \
    X(ResourceBundle::IndexType, BaseVertex, 0)                                                    \
    X(ResourceBundle::IndexType, MaterialIndex, ResourceBundle::kInvalidIndex)                     \
    X(BoundingBox, BoundingBox)

struct MeshResource final
{
    MESH_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<MeshResource>);
MLG_ASSERT_FIELD_COUNT(MESH_RESOURCE_FIELDS, 5);
MLG_ASSERT_NO_PADDING(MeshResource, MESH_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(MeshResource, IndexCount, 0)
MLG_ASSERT_OFFSET(MeshResource, FirstIndex, 4)
MLG_ASSERT_OFFSET(MeshResource, BaseVertex, 8)
MLG_ASSERT_OFFSET(MeshResource, MaterialIndex, 12)
MLG_ASSERT_OFFSET(MeshResource, BoundingBox, 16)
MLG_ASSERT_SIZE(MeshResource, 40)

/// ModelResource

#define MODEL_RESOURCE_FIELDS(X)                                                                   \
    X(ResourceBundle::IndexType, FirstMeshIndex, 0)                                                \
    X(ResourceBundle::CountType, MeshCount, 0)                                                     \
    X(BoundingBox, BoundingBox)

struct ModelResource final
{
    MODEL_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<ModelResource>);
MLG_ASSERT_FIELD_COUNT(MODEL_RESOURCE_FIELDS, 3);
MLG_ASSERT_NO_PADDING(ModelResource, MODEL_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ModelResource, FirstMeshIndex, 0)
MLG_ASSERT_OFFSET(ModelResource, MeshCount, 4)
MLG_ASSERT_OFFSET(ModelResource, BoundingBox, 8)
MLG_ASSERT_SIZE(ModelResource, 32)

/// ModelInstanceResource

#define MODEL_INSTANCE_RESOURCE_FIELDS(X)                                                          \
    X(ResourceBundle::IndexType, NodeIndex, ResourceBundle::kInvalidIndex)                         \
    X(ResourceBundle::IndexType, ModelIndex, ResourceBundle::kInvalidIndex)

struct ModelInstanceResource final
{
    MODEL_INSTANCE_RESOURCE_FIELDS(MLG_DECLARE_FIELD)
};
static_assert(BinaryStruct<ModelInstanceResource>);
MLG_ASSERT_FIELD_COUNT(MODEL_INSTANCE_RESOURCE_FIELDS, 2);
MLG_ASSERT_NO_PADDING(ModelInstanceResource, MODEL_INSTANCE_RESOURCE_FIELDS);
MLG_ASSERT_OFFSET(ModelInstanceResource, NodeIndex, 0)
MLG_ASSERT_OFFSET(ModelInstanceResource, ModelIndex, 4)
MLG_ASSERT_SIZE(ModelInstanceResource, 8)

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
    X(ResourceBundle::IndexType, NodeIndex, ResourceBundle::kInvalidIndex)                         \
    X(float, Mass, 0.0f)                                                                           \
    X(MotionType, MotionType)                                                                      \
    X(ResourceBundle::IndexType, FirstColliderIndex, 0)                                            \
    X(ResourceBundle::CountType, ColliderCount, 0)

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
MLG_ASSERT_OFFSET(RigidBodyResource, FirstColliderIndex, 12)
MLG_ASSERT_OFFSET(RigidBodyResource, ColliderCount, 16)
MLG_ASSERT_SIZE(RigidBodyResource, 20)

/// LevelNodeResource

#define LEVEL_NODE_RESOURCE_FIELDS(X)                                                              \
    X(ResourceBundle::IndexType, ParentIndex, ResourceBundle::kInvalidIndex)                       \
    X(ResourceBundle::IndexType, FirstChildIndex, ResourceBundle::kInvalidIndex)                   \
    X(ResourceBundle::CountType, ChildCount, 0)                                                    \
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