#include "ResourceBundle.h"

#include "LevelDefs.h"
#include "Result.h"

#include <cstddef>
#include <functional>
#include <map>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <SDL3/SDL_stdinc.h>

namespace
{

using MeshDefRef = std::reference_wrapper<const MeshDef>;
using NodeDefPointer = std::variant<const RootNodeDef*, const ChildNodeDef*>;
using IndexType = ResourceBundle::IndexType;
using OffsetType = ResourceBundle::OffsetType;
using Header = ResourceBundle::Header;

constexpr IndexType kInvalidIndex = ResourceBundle::kInvalidIndex;
constexpr OffsetType kInvalidOffset = ResourceBundle::kInvalidOffset;
constexpr size_t kMaxBundleSize = ResourceBundle::kMaxBundleSize;
constexpr size_t kMaxOffset = ResourceBundle::kMaxOffset;

template<typename T>
constexpr size_t kMaxVectorSize = std::min(ResourceBundle::kMaxCount, std::vector<T>().max_size());

struct FlatNodeDef
{
    NodeDefPointer NodeDefPtr;
    IndexType ParentIndex;
    IndexType FirstChildIndex;
};

template<typename T>
struct IndexEntry
{
    T Value;
    size_t Index;
};

template<typename T>
class Collection
{
public:
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "T must have standard layout");
    static_assert(sizeof(T) < kMaxOffset);

    using ValueType = T;

    explicit Collection(const std::span<const T> span)
        : m_Storage(span)
    {
    }

    explicit Collection(const std::vector<T>& vector)
        : Collection(std::span(vector))
    {
    }

    explicit Collection(std::vector<T>&& vector)
        : m_Storage(std::move(vector))
    {
    }

    explicit Collection(const std::vector<T>&& vector) = delete;

    template<typename K>
    explicit Collection(const std::map<K, T>& map)
    {
        std::vector<T> vector;
        vector.reserve(map.size());
        for(const auto& [key, value] : map)
        {
            vector.push_back(value);
        }
        m_Storage = std::move(vector);
    }

    template<typename K>
    explicit Collection(const std::map<K, IndexEntry<T>>& map)
    {
        std::vector<T> vector;
        vector.reserve(map.size());
        for(const auto& [key, indexEntry] : map)
        {
            vector.push_back(indexEntry.Value);
        }
        m_Storage = std::move(vector);
    }

    std::span<const T> GetSpan() const
    {
        return std::visit([](const auto& storage) -> std::span<const T>
            { return std::span<const T>(storage); },
            m_Storage);
    }

    size_t size() const { return GetSpan().size(); }

private:
    std::variant<std::span<const T>, std::vector<T>> m_Storage;
};

template<typename T>
constexpr size_t
Pad(const size_t size)
{
    return (alignof(T) - (size % alignof(T))) % alignof(T);
}

/// Pads the given buffer to ensure it is properly aligned for type T.
template<typename T>
void AppendPad(std::vector<std::byte>& buffer)
{
    const size_t pad = Pad<T>(buffer.size());
    buffer.insert(buffer.end(), pad, std::byte{0});
}

Result<std::vector<FlatNodeDef>>
FlattenNodesBreadthFirst(const std::span<const RootNodeDef> rootNodeDefs)
{
    struct PendingNode
    {
        NodeDefPointer Node;
        IndexType ParentIndex;
    };

    std::vector<FlatNodeDef> flatNodes;
    std::vector<PendingNode> pendingNodes;

    constexpr size_t kMaxNodeCount =
        std::min(kMaxVectorSize<FlatNodeDef>, kMaxVectorSize<PendingNode>);

    // Queue root nodes for processing.
    for(const RootNodeDef& rootNodeDef : rootNodeDefs)
    {
        MLG_CHECKV(pendingNodes.size() < kMaxNodeCount, "Too many nodes defined");
        pendingNodes.emplace_back(&rootNodeDef, kInvalidIndex);
    }

    for(size_t pendingIndex = 0; pendingIndex < pendingNodes.size(); ++pendingIndex)
    {
        const PendingNode& pendingNode = pendingNodes[pendingIndex];

        const size_t curNodeIndex = flatNodes.size();

        MLG_CHECKV(curNodeIndex < kMaxNodeCount, "Too many nodes defined");

        const FlatNodeDef flatNodeDef //
            {
                .NodeDefPtr = pendingNode.Node,
                .ParentIndex = pendingNode.ParentIndex,
                .FirstChildIndex = kInvalidIndex,
            };

        flatNodes.push_back(flatNodeDef);

        // Queue child nodes for processing.
        const auto visitResult = std::visit(
            [&](const auto* nodeDef) -> Result<>
            {
                for(const ChildNodeDef& childNodeDef : nodeDef->Children)
                {
                    MLG_CHECK(pendingNodes.size() < kMaxNodeCount, "Too many nodes defined");

                    const PendingNode pendingChildNode //
                        {
                            .Node = &childNodeDef,
                            .ParentIndex = static_cast<IndexType>(curNodeIndex),
                        };

                    pendingNodes.push_back(pendingChildNode);
                }

                return Result<>::Ok;
            },
            pendingNode.Node);

        MLG_CHECK(visitResult);
    }

    // Populate the FirstChildIndex for each parent node.
    IndexType parentIndex = kInvalidIndex;

    for(size_t nodeIndex = 0; nodeIndex < flatNodes.size(); ++nodeIndex)
    {
        const FlatNodeDef& flatNode = flatNodes[nodeIndex];
        if(flatNode.ParentIndex != parentIndex)
        {
            parentIndex = flatNode.ParentIndex;
            FlatNodeDef& parentNode = flatNodes[parentIndex];
            parentNode.FirstChildIndex = static_cast<IndexType>(nodeIndex);
        }
    }

    return flatNodes;
}

Result<std::vector<MeshDefRef>>
CollectMeshDefs(const std::span<const ModelDef> modelDefs)
{
    constexpr size_t kMaxMeshCount = kMaxVectorSize<MeshDef>;

    size_t count = 0;
    for(const ModelDef& modelDef : modelDefs)
    {
        const size_t meshCount = modelDef.MeshDefs.size();
        MLG_CHECKV(meshCount > 0, "Model has no meshes: {}", modelDef.Name);
        MLG_CHECKV(meshCount <= kMaxMeshCount, "Mesh count out of range: {}", modelDef.Name);
        MLG_CHECKV(kMaxMeshCount - count >= meshCount,
            "Mesh count out of range: {}",
            modelDef.Name);
        count += meshCount;
    }

    std::vector<std::reference_wrapper<const MeshDef>> meshDefs;
    static_assert(kMaxMeshCount <= meshDefs.max_size());
    meshDefs.reserve(count);

    for(const ModelDef& modelDef : modelDefs)
    {
        for(const MeshDef& meshDef : modelDef.MeshDefs)
        {
            meshDefs.push_back(std::cref(meshDef));
        }
    }

    return meshDefs;
}

Result<StringResource>
AddString(std::map<std::string_view, StringResource>& stringResourceMap,
    std::vector<char>& chars,
    const std::string_view& str)
{
    constexpr size_t kMaxCharCount = kMaxVectorSize<char>;
    const size_t kMaxStringCount =
        std::min(kMaxVectorSize<StringResource>, stringResourceMap.max_size());

    MLG_CHECKV(!str.empty(), "String is empty");

    MLG_CHECKV(!stringResourceMap.contains(str), "String already exists: {}", str);

    const size_t strLen = str.length();
    const size_t charIndex = chars.size();
    MLG_CHECKV(strLen <= kMaxCharCount, "String length out of bounds: {}", str);
    MLG_CHECKV(kMaxCharCount - charIndex >= strLen, "Char count out of bounds: {}", str);
    MLG_CHECKV(stringResourceMap.size() < kMaxStringCount, "String count out of range: {}", str);

    const StringResource sr = StringResource //
        {
            .CharIndex = static_cast<IndexType>(charIndex),
            .Length = static_cast<IndexType>(strLen),
        };

    chars.append_range(str);

    stringResourceMap.emplace(str, sr);

    return sr;
}

Result<std::map<std::string_view, StringResource>>
CollectStrings(const std::span<const FlatNodeDef> flatNodeDefs,
    const std::span<const MeshDefRef> meshDefs,
    std::vector<char>& chars)
{
    std::map<std::string_view, StringResource> stringResourceMap;

    chars.clear();

    // Collect unique strings from node names.
    for(const FlatNodeDef& flatNodeDef : flatNodeDefs)
    {
        const Result<> result = std::visit(
            [&](const auto* nodeDef) -> Result<>
            {
                if(!stringResourceMap.contains(nodeDef->Name))
                {
                    MLG_CHECK(AddString(stringResourceMap, chars, nodeDef->Name));
                }
                return Result<>::Ok;
            },
            flatNodeDef.NodeDefPtr);

        MLG_CHECK(result);
    }

    // Collect unique strings from texture paths.
    for(const MeshDefRef& meshDefRef : meshDefs)
    {
        const MeshDef& meshDef = meshDefRef.get();
        if(!meshDef.MaterialDef.BaseTexturePath.empty()
            && !stringResourceMap.contains(meshDef.MaterialDef.BaseTexturePath))
        {
            MLG_CHECK(AddString(stringResourceMap, chars, meshDef.MaterialDef.BaseTexturePath));
        }
    }

    return stringResourceMap;
}
Result<std::map<std::string_view, IndexEntry<TextureResource>>>
CollectTextures(const std::span<const MeshDefRef> meshDefs,
    const std::map<std::string_view, StringResource>& stringIndexMap)
{
    std::map<std::string_view, IndexEntry<TextureResource>> textureIndexMap;

    const size_t kMaxTextureCount =
        std::min(kMaxVectorSize<TextureResource>, textureIndexMap.max_size());

    for(const MeshDefRef& meshDefRef : meshDefs)
    {
        const MeshDef& meshDef = meshDefRef.get();
        const std::string_view texPath = meshDef.MaterialDef.BaseTexturePath;
        if(texPath.empty())
        {
            continue;
        }

        if(textureIndexMap.contains(texPath))
        {
            continue;
        }

        const auto it = stringIndexMap.find(texPath);

        // The string resource map was built from the texture paths so the
        // lookup should succeed 100%.
        MLG_ASSERT(it != stringIndexMap.end());

        const auto& stringRsrc = it->second;

        const TextureResource texRsrc //
            {
                .TexturePath = stringRsrc,
            };

        MLG_CHECKV(textureIndexMap.size() < kMaxTextureCount, "Texture index out of range");

        // Index will be filled later
        textureIndexMap.emplace(texPath, texRsrc);
    }

    size_t index = 0;
    for(auto& [texPath, entry] : textureIndexMap)
    {
        // Fill in the index.
        entry.Index = index++;
    }

    return textureIndexMap;
}

Result<std::map<MaterialDef, IndexEntry<MaterialResource>>>
CollectMaterials(const std::span<const MeshDefRef> meshDefs,
    const std::map<std::string_view, IndexEntry<TextureResource>>& textureIndexMap)
{
    std::map<MaterialDef, IndexEntry<MaterialResource>> materialIndexMap;

    const size_t kMaxMaterialCount =
        std::min(kMaxVectorSize<MaterialResource>, materialIndexMap.max_size());

    for(const MeshDefRef& meshDefRef : meshDefs)
    {
        const MeshDef& meshDef = meshDefRef.get();
        if(!materialIndexMap.contains(meshDef.MaterialDef))
        {
            MLG_CHECKV(materialIndexMap.size() < kMaxMaterialCount, "Material index out of range");

            const MaterialDef& materialDef = meshDef.MaterialDef;

            IndexType baseTextureIndex = kInvalidIndex;

            const std::string_view texPath = materialDef.BaseTexturePath;

            if(!texPath.empty())
            {
                auto it = textureIndexMap.find(texPath);
                MLG_CHECKV(it != textureIndexMap.end(), "Texture not found: {}", texPath);
                const IndexEntry<TextureResource>& entry = it->second;
                baseTextureIndex = static_cast<IndexType>(entry.Index);
            }

            const MaterialResource materialResource //
                {
                    .BaseTextureIndex = baseTextureIndex,
                    .Color = materialDef.Color,
                    .Metalness = materialDef.Metalness,
                    .Roughness = materialDef.Roughness,
                };

            // Index will be filled later
            materialIndexMap.emplace(meshDef.MaterialDef, materialResource);
        }
    }

    size_t index = 0;
    for(auto& [mtlDef, entry] : materialIndexMap)
    {
        // Fill in the index.
        entry.Index = index++;
    }

    return materialIndexMap;
}

Result<std::vector<Vertex>>
CollectVertices(const std::span<const MeshDefRef> meshDefs)
{
    constexpr size_t kMaxVertexCount = kMaxVectorSize<Vertex>;

    size_t count = 0;
    for(const MeshDefRef& meshDefRef : meshDefs)
    {
        const MeshDef& meshDef = meshDefRef.get();
        const size_t vtxCount = meshDef.Vertices.size();
        MLG_CHECKV(vtxCount > 0, "Vertex count must be greater than zero");
        MLG_CHECKV(vtxCount <= kMaxVertexCount, "Vertex count out of range");
        MLG_CHECKV(kMaxVertexCount - count >= vtxCount, "Vertex count out of range");
        count += vtxCount;
    }

    std::vector<Vertex> vertices;
    vertices.reserve(count);

    for(const MeshDefRef& meshDefRef : meshDefs)
    {
        const MeshDef& meshDef = meshDefRef.get();
        vertices.append_range(meshDef.Vertices);
    }

    return vertices;
}

Result<std::vector<VertexIndex>>
CollectIndices(const std::span<const MeshDefRef> meshDefs)
{
    constexpr size_t kMaxIndexCount = kMaxVectorSize<VertexIndex>;

    size_t count = 0;
    for(const MeshDefRef& meshDefRef : meshDefs)
    {
        const size_t idxCount = meshDefRef.get().Indices.size();
        MLG_CHECKV(idxCount >= 3, "Index count must be at least three");
        MLG_CHECKV(idxCount <= kMaxIndexCount, "Index count out of range");
        MLG_CHECKV(kMaxIndexCount - count >= idxCount, "Index count out of range");
        count += idxCount;
    }

    std::vector<VertexIndex> indices;
    indices.reserve(count);

    for(const MeshDefRef& meshDefRef : meshDefs)
    {
        const MeshDef& meshDef = meshDefRef.get();
        for(const VertexIndex index : meshDef.Indices)
        {
            MLG_CHECKV(index < meshDef.Vertices.size(), "Invalid vertex index");
        }

        indices.append_range(meshDef.Indices);
    }

    return indices;
}

Result<std::vector<MeshResource>>
CollectMeshes(const std::span<const MeshDefRef> meshDefs,
    const std::map<MaterialDef, IndexEntry<MaterialResource>>& materialIndexMap)
{
    constexpr size_t kMaxMeshCount = kMaxVectorSize<MeshResource>;

    MLG_CHECKV(meshDefs.size() <= kMaxMeshCount, "Mesh count out of range");

    std::vector<MeshResource> meshes;
    meshes.reserve(meshDefs.size());

    size_t indexIndex = 0;
    size_t vertexIndex = 0;

    for(const MeshDefRef& meshDefRef : meshDefs)
    {
        const MeshDef& meshDef = meshDefRef.get();
        const BoundingBox boundingBox =
            BoundingBox::FromVertices(meshDef.Vertices, meshDef.Indices);

        const auto it = materialIndexMap.find(meshDef.MaterialDef);
        MLG_CHECKV(it != materialIndexMap.end(), "Material not found");
        const IndexEntry<MaterialResource>& entry = it->second;

        // At this point vertices/indices/materials have been confirmed to not
        // exceed the max count so no checking is needed.
        const size_t indexCount = meshDef.Indices.size();
        const size_t vertexCount = meshDef.Vertices.size();
        const size_t materialIndex = entry.Index;

        const MeshResource mesh //
            {
                .IndexCount = static_cast<IndexType>(indexCount),
                .FirstIndex = static_cast<IndexType>(indexIndex),
                .BaseVertex = static_cast<IndexType>(vertexIndex),
                .MaterialIndex = static_cast<IndexType>(materialIndex),
                .BoundingBox = boundingBox,
            };
        meshes.push_back(mesh);

        indexIndex += indexCount;
        vertexIndex += vertexCount;
    }

    return meshes;
}

Result<std::map<std::string_view, IndexEntry<ModelResource>>>
CollectModels(const std::span<const ModelDef> modelDefs, const std::span<const MeshResource> meshes)
{
    std::map<std::string_view, IndexEntry<ModelResource>> modelIndexMap;

    const size_t kMaxModelCount = std::min(kMaxVectorSize<ModelResource>, modelIndexMap.max_size());

    MLG_CHECKV(modelDefs.size() <= kMaxModelCount, "Model count out of range");

    size_t meshIndex = 0;

    // Meshes are collected sequentially for each model.
    // I.e. meshes for model N+1 immediately follow those for model N.

    for(const ModelDef& modelDef : modelDefs)
    {
        // At this point mesh counts have been confirmed to not
        // exceed the max, so no checking is needed.
        const size_t meshCount = modelDef.MeshDefs.size();

        MLG_CHECKV(meshCount > 0, "Model has no meshes: {}", modelDef.Name);

        const std::span meshSpan = meshes.subspan(meshIndex, meshCount);

        BoundingBox boundingBox = meshSpan[0].BoundingBox;
        for(const MeshResource& mesh : meshSpan.subspan(1))
        {
            boundingBox += mesh.BoundingBox;
        }

        const std::string_view modelName = modelDef.Name;

        MLG_CHECKV(!modelName.empty(), "Model name is empty");

        MLG_CHECKV(!modelIndexMap.contains(modelName), "Duplicate model name: {}", modelName);
        MLG_CHECKV(modelIndexMap.size() < kMaxModelCount, "Model index out of range");

        const ModelResource model //
            {
                .FirstMeshIndex = static_cast<IndexType>(meshIndex),
                .MeshCount = static_cast<IndexType>(meshCount),
                .BoundingBox = boundingBox,
            };

        // Index will be filled later
        modelIndexMap.emplace(modelName, model);

        meshIndex += meshCount;
    }

    size_t index = 0;
    for(auto& [modelName, entry] : modelIndexMap)
    {
        // Fill in the index.
        entry.Index = index++;
    }

    return modelIndexMap;
}

Result<std::vector<ModelInstanceResource>>
CollectModelInstances(const std::span<const FlatNodeDef> flatNodeDefs,
    const std::map<std::string_view, IndexEntry<ModelResource>>& modelIndexMap)
{
    constexpr size_t kMaxModelInstanceCount = kMaxVectorSize<ModelInstanceResource>;

    size_t count = 0;
    for(const FlatNodeDef& flatNodeDef : flatNodeDefs)
    {
        const bool hasModel =
            std::visit([&](const auto* nodeDef) { return static_cast<bool>(nodeDef->Model); },
                flatNodeDef.NodeDefPtr);
        if(hasModel)
        {
            MLG_CHECKV(count < kMaxModelInstanceCount, "Model instance count out of range");
            ++count;
        }
    }

    std::vector<ModelInstanceResource> modelInstances;
    modelInstances.reserve(count);

    for(size_t nodeIndex = 0; nodeIndex < flatNodeDefs.size(); ++nodeIndex)
    {
        const FlatNodeDef& flatNodeDef = flatNodeDefs[nodeIndex];

        const auto result = std::visit(
            [&](const auto* nodeDef) -> Result<>
            {
                const std::optional<ModelRef>& optModel = nodeDef->Model;

                if(optModel)
                {
                    const ModelRef& modelRef = *optModel;
                    auto it = modelIndexMap.find(modelRef.Name);
                    MLG_CHECKV(it != modelIndexMap.end(), "Model not found: {}", modelRef.Name);
                    const IndexEntry<ModelResource>& modelEntry = it->second;

                    const size_t modelIdx = modelEntry.Index;

                    const ModelInstanceResource modelInstance //
                        {
                            .NodeIndex = static_cast<IndexType>(nodeIndex),
                            .ModelIndex = static_cast<IndexType>(modelIdx),
                        };

                    modelInstances.push_back(modelInstance);
                }

                return Result<>::Ok;
            },
            flatNodeDef.NodeDefPtr);

        MLG_CHECK(result);
    }

    return modelInstances;
}

ColliderResource
CreateCollider(const ColliderDef& colliderDef)
{
    switch(colliderDef.Shape.GetType())
    {
        case ColliderShapeType::Sphere:
        {
            const SphereDef& sphereDef = colliderDef.Shape.GetSphere();
            return ColliderResource //
                {
                    .CollisionType = colliderDef.CollisionType,
                    .ShapeType = colliderDef.Shape.GetType(),
                    .Shape =
                        ColliderResource::Sphere //
                    {
                        .Radius = sphereDef.Radius,
                        .Center = sphereDef.Center,
                    },
                };
        }
        case ColliderShapeType::Box:
        {
            const BoxDef& boxDef = colliderDef.Shape.GetBox();
            return ColliderResource //
                {
                    .CollisionType = colliderDef.CollisionType,
                    .ShapeType = colliderDef.Shape.GetType(),
                    .Shape =
                        ColliderResource::Box //
                    {
                        .HalfExtents = boxDef.HalfExtents,
                        .Center = boxDef.Center,
                    },
                };
        }
        case ColliderShapeType::Capsule:
        {
            const CapsuleDef& capsuleDef = colliderDef.Shape.GetCapsule();
            return ColliderResource //
                {
                    .CollisionType = colliderDef.CollisionType,
                    .ShapeType = colliderDef.Shape.GetType(),
                    .Shape =
                        ColliderResource::Capsule //
                    {
                        .Radius = capsuleDef.Radius,
                        .HalfHeight = capsuleDef.HalfHeight,
                        .Center = capsuleDef.Center,
                    },
                };
        }
        default:
            MLG_ABORT("Unsupported collider shape type");
    }
}

Result<std::vector<ColliderResource>>
CollectColliders(const std::span<const RootNodeDef> nodeDefs)
{
    constexpr size_t kMaxColliderCount = kMaxVectorSize<ColliderResource>;

    size_t count = 0;
    for(const RootNodeDef& nodeDef : nodeDefs)
    {
        if(nodeDef.Body)
        {
            const size_t colliderCount = nodeDef.Body->Colliders.size();
            MLG_CHECKV(colliderCount <= kMaxColliderCount, "Collider count out of range");
            MLG_CHECKV(kMaxColliderCount - count >= colliderCount, "Collider count out of range");
            count += nodeDef.Body->Colliders.size();
        }
    }

    std::vector<ColliderResource> colliders;
    colliders.reserve(count);

    for(const RootNodeDef& nodeDef : nodeDefs)
    {
        const std::optional<RigidBodyDef> body = nodeDef.Body;
        if(!body)
        {
            continue;
        }

        MLG_CHECKV(!body->Colliders.empty(), "Rigid body has no colliders");

        for(const ColliderDef& colliderDef : body->Colliders)
        {
            colliders.push_back(CreateCollider(colliderDef));
        }
    }

    return colliders;
}

Result<std::vector<RigidBodyResource>>
CollectRigidBodies(const std::span<const RootNodeDef> nodeDefs)
{
    constexpr size_t kMaxRigidBodyCount = kMaxVectorSize<RigidBodyResource>;

    size_t count = 0;
    for(const RootNodeDef& nodeDef : nodeDefs)
    {
        if(nodeDef.Body)
        {
            MLG_CHECKV(count < kMaxRigidBodyCount, "Rigid body count out of range");
            ++count;
        }
    }

    std::vector<RigidBodyResource> rigidBodies;
    rigidBodies.reserve(count);

    size_t colliderIndex = 0;

    for(size_t nodeIndex = 0; nodeIndex < nodeDefs.size(); ++nodeIndex)
    {
        const RootNodeDef& nodeDef = nodeDefs[nodeIndex];

        const std::optional<RigidBodyDef> body = nodeDef.Body;
        if(!body)
        {
            continue;
        }

        const size_t colliderCount = body->Colliders.size();

        MLG_CHECKV(colliderCount > 0, "Rigid body has no colliders");

        const RigidBodyResource rigidBody //
            {
                .NodeIndex = static_cast<IndexType>(nodeIndex),
                .Mass = body->Mass.Value(),
                .MotionType = body->MotionType,
                .FirstColliderIndex = static_cast<IndexType>(colliderIndex),
                .ColliderCount = static_cast<IndexType>(colliderCount),
            };

        rigidBodies.push_back(rigidBody);

        colliderIndex += colliderCount;
    }

    return rigidBodies;
}

Result<std::vector<LevelNodeResource>>
CollectLevelNodes(const std::span<const FlatNodeDef> flatNodeDefs,
    const std::map<std::string_view, StringResource>& stringIndexMap)
{
    constexpr size_t kMaxLevelNodeCount = kMaxVectorSize<LevelNodeResource>;

    MLG_CHECKV(flatNodeDefs.size() <= kMaxLevelNodeCount, "Level node count out of range");

    std::vector<LevelNodeResource> levelNodes;
    levelNodes.reserve(flatNodeDefs.size());

    for(const FlatNodeDef& flatNodeDef : flatNodeDefs)
    {
        const auto result = std::visit(
            [&](const auto* nodeDef) -> Result<>
            {
                auto it = stringIndexMap.find(nodeDef->Name);
                MLG_CHECKV(it != stringIndexMap.end(), "Node name not found: {}", nodeDef->Name);

                const auto& stringRsrc = it->second;
                const size_t childCount = nodeDef->Children.size();

                const LevelNodeResource levelNode //
                    {
                        .Name = stringRsrc,
                        .ParentIndex = flatNodeDef.ParentIndex,
                        .FirstChildIndex = flatNodeDef.FirstChildIndex,
                        .ChildCount = static_cast<IndexType>(childCount),
                        .LocalPos = nodeDef->Transform.T,
                        .LocalRot = nodeDef->Transform.R.ToVector(),
                        .LocalScale = nodeDef->Transform.S,
                    };

                // At this point it's been confirmed that flatNodeDefs does not
                // exceed the max cout so no bounds checking needed.
                levelNodes.push_back(levelNode);

                return Result<>::Ok;
            },
            flatNodeDef.NodeDefPtr);

        MLG_CHECK(result);
    }

    return levelNodes;
}

const Header*
GetHeader(const std::span<const std::byte>& buffer)
{
    if(!MLG_VERIFY(buffer.size() >= sizeof(Header)))
    {
        return nullptr;
    }

    const void* p = buffer.data();
    return static_cast<const Header*>(p);
}

template<typename T>
std::span<const std::byte>
AsBytes(const T& value)
{
    const void* p = &value;
    return std::span(static_cast<const std::byte*>(p), sizeof(T));
}

template<typename T>
std::span<const std::byte>
AsBytes(const std::span<const T> span)
{
    return std::as_bytes(span);
}

template<typename T>
Result<>
AppendSpan(const std::span<const T> span, std::vector<std::byte>& buffer)
{
    const Header* header = GetHeader(buffer);
    MLG_CHECKV(header != nullptr, "Header is not initialized");

    const std::span bytes = AsBytes(span);

    MLG_CHECKV(buffer.size() <= header->TotalSize, "Buffer size exceeds bundle capacity");
    MLG_CHECKV(header->TotalSize - buffer.size() >= bytes.size(),
        "Appending span exceeds bundle capacity");

    buffer.insert(buffer.end(), bytes.begin(), bytes.end());

    return Result<>::Ok;
}

std::string_view
MakeStringView(const StringResource& resource, const std::span<const char>& chars)
{
    if(!MLG_VERIFY((resource.CharIndex < chars.size())
               && (chars.size() - resource.CharIndex >= resource.Length),
           "Invalid string resource"))
    {
        return std::string_view();
    }

    return std::string_view(chars.subspan(resource.CharIndex, resource.Length));
}

uint32_t
GetChecksum(const std::span<const std::byte>& buffer)
{
    const Header* header = GetHeader(buffer);
    if(!MLG_VERIFY(header != nullptr, "Header is not initialized"))
    {
        return 0;
    }

    static_assert(offsetof(Header, Checksum) == 0, "Checksum must be at offset 0");

    constexpr size_t crcStartOffset = offsetof(Header, Checksum) + sizeof(Header::Checksum);

    const void* crcStart = &buffer[crcStartOffset];
    const size_t crcLen = buffer.size() - crcStartOffset;
    return SDL_crc32(0, crcStart, crcLen);
}

} // namespace

// ResourceBundle

bool
ResourceBundle::ValidateChecksum() const
{
    const Header* header = GetHeader();

    if(!MLG_VERIFY(header != nullptr, "Header is not initialized"))
    {
        return false;
    }

    return ::GetChecksum(m_Buffer) == header->Checksum;
}

std::string_view
ResourceBundle::GetStringView(const StringResource& stringResource) const
{
    return MakeStringView(stringResource, GetChars());
}

// ResourceBundleBuilder

Result<ResourceBundle>
ResourceBundleBuilder::Build(const LevelDef& levelDef, const PropKitDef& propKitDef)
{
    // Free buffer mem
    std::vector<std::byte>().swap(m_Buffer);

    const auto flatNodeDefs = FlattenNodesBreadthFirst(levelDef.NodeDefs);
    MLG_CHECK(flatNodeDefs);

    const auto meshDefs = CollectMeshDefs(propKitDef.ModelDefs);
    MLG_CHECK(meshDefs);

    std::vector<char> chars;

    const auto stringIndexMap = CollectStrings(*flatNodeDefs, *meshDefs, chars);
    MLG_CHECK(stringIndexMap);
    const auto textureIndexMap = CollectTextures(*meshDefs, *stringIndexMap);
    MLG_CHECK(textureIndexMap);
    const auto materialIndexMap = CollectMaterials(*meshDefs, *textureIndexMap);
    MLG_CHECK(materialIndexMap);
    const auto vertices = CollectVertices(*meshDefs);
    MLG_CHECK(vertices);
    const auto indices = CollectIndices(*meshDefs);
    MLG_CHECK(indices);
    const auto meshes = CollectMeshes(*meshDefs, *materialIndexMap);
    MLG_CHECK(meshes);
    const auto modelIndexMap = CollectModels(propKitDef.ModelDefs, *meshes);
    MLG_CHECK(modelIndexMap);
    const auto modelInstances = CollectModelInstances(*flatNodeDefs, *modelIndexMap);
    MLG_CHECK(modelInstances);
    const auto colliders = CollectColliders(levelDef.NodeDefs);
    MLG_CHECK(colliders);
    const auto rigidBodies = CollectRigidBodies(levelDef.NodeDefs);
    MLG_CHECK(rigidBodies);
    const auto nodes = CollectLevelNodes(*flatNodeDefs, *stringIndexMap);
    MLG_CHECK(nodes);

    const auto charCollection = Collection(chars);
    const auto stringCollection = Collection(*stringIndexMap);
    const auto textureCollection = Collection(*textureIndexMap);
    const auto materialCollection = Collection(*materialIndexMap);
    const auto modelCollection = Collection(*modelIndexMap);
    const auto vertexCollection = Collection(*vertices);
    const auto indexCollection = Collection(*indices);
    const auto meshCollection = Collection(*meshes);
    const auto modelInstanceCollection = Collection(*modelInstances);
    const auto colliderCollection = Collection(*colliders);
    const auto rigidBodyCollection = Collection(*rigidBodies);
    const auto nodeCollection = Collection(*nodes);

    size_t totalSize = sizeof(Header);

    auto addAndCheckOverflow = [&](const auto& collection) -> Result<>
    {
        using CollectionType = std::decay_t<decltype(collection)>;
        using ValueType = CollectionType::ValueType;

        const size_t pad = Pad<ValueType>(totalSize);
        const size_t remaining = kMaxBundleSize - totalSize;

        MLG_CHECK(pad <= remaining, "Padding size exceeds maximum bundle size");
        MLG_CHECKV((remaining - pad) / sizeof(ValueType) >= collection.size(),
            "Collection size would overflow maximum allowable size");

        const size_t byteSizeOfCollection = (collection.size() * sizeof(ValueType)) + pad;

        totalSize += byteSizeOfCollection;

        return Result<>::Ok;
    };

    MLG_CHECK(addAndCheckOverflow(charCollection));
    MLG_CHECK(addAndCheckOverflow(stringCollection));
    MLG_CHECK(addAndCheckOverflow(textureCollection));
    MLG_CHECK(addAndCheckOverflow(materialCollection));
    MLG_CHECK(addAndCheckOverflow(vertexCollection));
    MLG_CHECK(addAndCheckOverflow(indexCollection));
    MLG_CHECK(addAndCheckOverflow(meshCollection));
    MLG_CHECK(addAndCheckOverflow(modelCollection));
    MLG_CHECK(addAndCheckOverflow(modelInstanceCollection));
    MLG_CHECK(addAndCheckOverflow(colliderCollection));
    MLG_CHECK(addAndCheckOverflow(rigidBodyCollection));
    MLG_CHECK(addAndCheckOverflow(nodeCollection));

    m_Buffer.reserve(totalSize);

    AppendHeader(static_cast<OffsetType>(totalSize));
    MLG_CHECK(Append(charCollection.GetSpan()));
    MLG_CHECK(Append(stringCollection.GetSpan()));
    MLG_CHECK(Append(textureCollection.GetSpan()));
    MLG_CHECK(Append(materialCollection.GetSpan()));
    MLG_CHECK(Append(vertexCollection.GetSpan()));
    MLG_CHECK(Append(indexCollection.GetSpan()));
    MLG_CHECK(Append(meshCollection.GetSpan()));
    MLG_CHECK(Append(modelCollection.GetSpan()));
    MLG_CHECK(Append(modelInstanceCollection.GetSpan()));
    MLG_CHECK(Append(colliderCollection.GetSpan()));
    MLG_CHECK(Append(rigidBodyCollection.GetSpan()));
    MLG_CHECK(Append(nodeCollection.GetSpan()));

    GetHeader()->Checksum = GetChecksum(m_Buffer);

    return ResourceBundle{ std::move(m_Buffer) };
}

// private:

void
ResourceBundleBuilder::AppendHeader(const OffsetType totalSize)
{
    MLG_ASSERT(m_Buffer.empty(), "Header already appended");

    Header header{};

    header.TotalSize = totalSize;

    m_Buffer.append_range(AsBytes(header));
}

Result<>
ResourceBundleBuilder::Append(const std::span<const char>& chars)
{
    Header* h = GetHeader();
    MLG_ASSERT(h != nullptr, "Header is not initialized");
    MLG_ASSERT(h->CharsOffset == kInvalidOffset, "Chars already appended");

    AppendPad<char>(m_Buffer);
    h->CharsOffset = static_cast<OffsetType>(m_Buffer.size());
    h->CharsLength = static_cast<IndexType>(chars.size());
    return AppendSpan(chars, m_Buffer);
}

Result<>
ResourceBundleBuilder::Append(const std::span<const StringResource>& strings)
{
    Header* h = GetHeader();
    MLG_ASSERT(h != nullptr, "Header is not initialized");
    MLG_ASSERT(h->StringsOffset == kInvalidOffset, "Strings already appended");

    AppendPad<StringResource>(m_Buffer);
    h->StringsOffset = static_cast<OffsetType>(m_Buffer.size());
    h->StringCount = static_cast<IndexType>(strings.size());
    return AppendSpan(strings, m_Buffer);
}

Result<>
ResourceBundleBuilder::Append(const std::span<const TextureResource>& textures)
{
    Header* h = GetHeader();
    MLG_ASSERT(h != nullptr, "Header is not initialized");
    MLG_ASSERT(h->TexturesOffset == kInvalidOffset, "Textures already appended");

    AppendPad<TextureResource>(m_Buffer);
    h->TexturesOffset = static_cast<OffsetType>(m_Buffer.size());
    h->TextureCount = static_cast<IndexType>(textures.size());
    return AppendSpan(textures, m_Buffer);
}

Result<>
ResourceBundleBuilder::Append(const std::span<const MaterialResource>& materials)
{
    Header* h = GetHeader();
    MLG_ASSERT(h != nullptr, "Header is not initialized");
    MLG_ASSERT(h->MaterialsOffset == kInvalidOffset, "Materials already appended");

    AppendPad<MaterialResource>(m_Buffer);
    h->MaterialsOffset = static_cast<OffsetType>(m_Buffer.size());
    h->MaterialCount = static_cast<IndexType>(materials.size());
    return AppendSpan(materials, m_Buffer);
}

Result<>
ResourceBundleBuilder::Append(const std::span<const Vertex>& vertices)
{
    Header* h = GetHeader();
    MLG_ASSERT(h != nullptr, "Header is not initialized");
    MLG_ASSERT(h->VerticesOffset == kInvalidOffset, "Vertices already appended");

    AppendPad<Vertex>(m_Buffer);
    h->VerticesOffset = static_cast<OffsetType>(m_Buffer.size());
    h->VertexCount = static_cast<IndexType>(vertices.size());
    return AppendSpan(vertices, m_Buffer);
}

Result<>
ResourceBundleBuilder::Append(const std::span<const VertexIndex>& indices)
{
    Header* h = GetHeader();
    MLG_ASSERT(h != nullptr, "Header is not initialized");
    MLG_ASSERT(h->IndicesOffset == kInvalidOffset, "Indices already appended");

    AppendPad<VertexIndex>(m_Buffer);
    h->IndicesOffset = static_cast<OffsetType>(m_Buffer.size());
    h->IndexCount = static_cast<IndexType>(indices.size());
    return AppendSpan(indices, m_Buffer);
}

Result<>
ResourceBundleBuilder::Append(const std::span<const MeshResource>& meshes)
{
    Header* h = GetHeader();
    MLG_ASSERT(h != nullptr, "Header is not initialized");
    MLG_ASSERT(h->MeshesOffset == kInvalidOffset, "Meshes already appended");

    AppendPad<MeshResource>(m_Buffer);
    h->MeshesOffset = static_cast<OffsetType>(m_Buffer.size());
    h->MeshCount = static_cast<IndexType>(meshes.size());
    return AppendSpan(meshes, m_Buffer);
}

Result<>
ResourceBundleBuilder::Append(const std::span<const ModelResource>& models)
{
    Header* h = GetHeader();
    MLG_ASSERT(h != nullptr, "Header is not initialized");
    MLG_ASSERT(h->ModelsOffset == kInvalidOffset, "Models already appended");

    AppendPad<ModelResource>(m_Buffer);
    h->ModelsOffset = static_cast<OffsetType>(m_Buffer.size());
    h->ModelCount = static_cast<IndexType>(models.size());
    return AppendSpan(models, m_Buffer);
}

Result<>
ResourceBundleBuilder::Append(const std::span<const ModelInstanceResource>& modelInstances)
{
    Header* h = GetHeader();
    MLG_ASSERT(h != nullptr, "Header is not initialized");
    MLG_ASSERT(h->ModelInstancesOffset == kInvalidOffset,
        "Model Instances already appended");

    AppendPad<ModelInstanceResource>(m_Buffer);
    h->ModelInstancesOffset = static_cast<OffsetType>(m_Buffer.size());
    h->ModelInstanceCount = static_cast<IndexType>(modelInstances.size());
    return AppendSpan(modelInstances, m_Buffer);
}

Result<>
ResourceBundleBuilder::Append(const std::span<const ColliderResource>& colliders)
{
    Header* h = GetHeader();
    MLG_ASSERT(h != nullptr, "Header is not initialized");
    MLG_ASSERT(h->CollidersOffset == kInvalidOffset, "Colliders already appended");

    AppendPad<ColliderResource>(m_Buffer);
    h->CollidersOffset = static_cast<OffsetType>(m_Buffer.size());
    h->ColliderCount = static_cast<IndexType>(colliders.size());
    return AppendSpan(colliders, m_Buffer);
}

Result<>
ResourceBundleBuilder::Append(const std::span<const RigidBodyResource>& rigidBodies)
{
    Header* h = GetHeader();
    MLG_ASSERT(h != nullptr, "Header is not initialized");
    MLG_ASSERT(h->RigidBodiesOffset == kInvalidOffset, "RigidBodies already appended");

    AppendPad<RigidBodyResource>(m_Buffer);
    h->RigidBodiesOffset = static_cast<OffsetType>(m_Buffer.size());
    h->RigidBodyCount = static_cast<IndexType>(rigidBodies.size());
    return AppendSpan(rigidBodies, m_Buffer);
}

Result<>
ResourceBundleBuilder::Append(const std::span<const LevelNodeResource>& nodes)
{
    Header* h = GetHeader();
    MLG_ASSERT(h != nullptr, "Header is not initialized");
    MLG_ASSERT(h->NodesOffset == kInvalidOffset, "Nodes already appended");

    AppendPad<LevelNodeResource>(m_Buffer);
    h->NodesOffset = static_cast<OffsetType>(m_Buffer.size());
    h->NodeCount = static_cast<IndexType>(nodes.size());
    return AppendSpan(nodes, m_Buffer);
}