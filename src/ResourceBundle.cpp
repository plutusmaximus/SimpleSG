#include "ResourceBundle.h"

#include "LevelDefs.h"

#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <string_view>

namespace
{

constexpr size_t kMaxSizeT = std::numeric_limits<size_t>::max();

using NodeDefPointer = std::variant<const RootNodeDef*, const ChildNodeDef*>;

struct FlatNodeDef
{
    NodeDefPointer NodeDefPtr;
    ResourceBundle::IndexType ParentIndex;
    ResourceBundle::IndexType FirstChildIndex;
};

[[nodiscard]] ResourceBundle::IndexType
IndexBoundsCheck(const size_t size)
{
    MLG_ABORTIF(size > ResourceBundle::kMaxIndex, "Index out of bounds");
    return static_cast<ResourceBundle::IndexType>(size);
}

[[nodiscard]] ResourceBundle::CountType
CountBoundsCheck(const size_t count)
{
    MLG_ABORTIF(count > ResourceBundle::kMaxCount, "Count out of bounds");
    return static_cast<ResourceBundle::CountType>(count);
}

[[nodiscard]] ResourceBundle::OffsetType
OffsetBoundsCheck(const size_t offset)
{
    MLG_ABORTIF(offset > ResourceBundle::kMaxOffset, "Offset out of bounds");
    return static_cast<ResourceBundle::OffsetType>(offset);
}

std::vector<FlatNodeDef>
FlattenNodesBreadthFirst(const std::span<const RootNodeDef> rootNodeDefs)
{
    struct PendingNode
    {
        NodeDefPointer Node;
        ResourceBundle::IndexType ParentIndex;
    };

    std::vector<FlatNodeDef> flatNodes;
    std::vector<PendingNode> pendingNodes;

    // Queue root nodes for processing.
    for(const RootNodeDef& rootNodeDef : rootNodeDefs)
    {
        pendingNodes.emplace_back(&rootNodeDef, ResourceBundle::kInvalidIndex);
    }

    for(size_t pendingIndex = 0; pendingIndex < pendingNodes.size(); ++pendingIndex)
    {
        const PendingNode& pendingNode = pendingNodes[pendingIndex];

        const ResourceBundle::IndexType nodeIndex = IndexBoundsCheck(flatNodes.size());

        const FlatNodeDef flatNodeDef //
            {
                .NodeDefPtr = pendingNode.Node,
                .ParentIndex = pendingNode.ParentIndex,
                .FirstChildIndex = ResourceBundle::kInvalidIndex,
            };

        flatNodes.push_back(flatNodeDef);

        // Queue child nodes for processing.
        std::visit(
            [&](const auto* nodeDef)
            {
                for(const ChildNodeDef& childNodeDef : nodeDef->Children)
                {
                    pendingNodes.emplace_back(&childNodeDef, nodeIndex);
                }
            },
            pendingNode.Node);
    }

    // Populate the FirstChildIndex for each parent node.
    ResourceBundle::IndexType parentIndex = ResourceBundle::kInvalidIndex;
    auto view = std::views::zip(flatNodes, std::views::iota(size_t{ 0 }));

    for(const auto& [flatNode, nodeIndex] : view)
    {
        if(flatNode.ParentIndex != parentIndex)
        {
            parentIndex = flatNode.ParentIndex;
            FlatNodeDef& parentNode = flatNodes[parentIndex];
            parentNode.FirstChildIndex = IndexBoundsCheck(nodeIndex);
        }
    }

    return flatNodes;
}

[[nodiscard]] StringResource
AddString(std::vector<char>& chars, const std::string_view& str)
{
    MLG_ABORTIF(chars.size() > ResourceBundle::kMaxBundleSize, "Exceeded maximum buffer size");

    using LengthType = decltype(ResourceBundle::kMaxBundleSize);

    const LengthType capacity =
        ResourceBundle::kMaxBundleSize - static_cast<LengthType>(chars.size());

    MLG_ABORTIF(capacity < str.length(), "Exceeded maximum buffer size");

    const ResourceBundle::IndexType charIndex = IndexBoundsCheck(chars.size());

    chars.append_range(str);

    return StringResource //
        {
            .CharIndex = charIndex,
            .Length = CountBoundsCheck(str.length()),
        };
}

std::vector<NodeNameResource>
CollectNodeNames(const std::span<const FlatNodeDef> flatNodeDefs, std::vector<char>& chars)
{
    std::vector<NodeNameResource> nodeNames;
    std::set<std::string_view> stringDedup;

    auto view = std::views::zip(flatNodeDefs, std::views::iota(size_t{ 0 }));

    for(const auto& [flatNodeDef, nodeIndex] : view)
    {
        std::visit(
            [&](const auto* nodeDef)
            {
                if(!stringDedup.contains(nodeDef->Name))
                {
                    const NodeNameResource nodeName //
                        {
                            .NodeIndex = IndexBoundsCheck(nodeIndex),
                            .String = AddString(chars, nodeDef->Name),
                        };

                    stringDedup.insert(nodeDef->Name);
                    nodeNames.push_back(nodeName);
                }
            },
            flatNodeDef.NodeDefPtr);
    }

    return nodeNames;
}

std::vector<StringResource>
CollectTextureUris(const std::span<const MeshDef> meshDefs, std::vector<char>& chars)
{
    std::vector<StringResource> textureUris;
    std::set<std::string_view> stringDedup;

    for(const MeshDef& meshDef : meshDefs)
    {
        const MaterialDef& materialDef = meshDef.MaterialDef;

        if(materialDef.BaseTextureUri.empty())
        {
            continue;
        }

        if(!stringDedup.contains(materialDef.BaseTextureUri))
        {
            const StringResource textureUri = AddString(chars, materialDef.BaseTextureUri);

            stringDedup.insert(materialDef.BaseTextureUri);
            textureUris.push_back(textureUri);
        }
    }

    return textureUris;
}

std::map<const MaterialDef, ResourceBundle::IndexType>
CreateMaterialIndexMap(const std::span<const MeshDef> meshDefs)
{
    std::map<const MaterialDef, ResourceBundle::IndexType> materialIndexMap;

    for(const MeshDef& meshDef : meshDefs)
    {
        const MaterialDef& materialDef = meshDef.MaterialDef;
        if(!materialIndexMap.contains(materialDef))
        {
            materialIndexMap[materialDef] = IndexBoundsCheck(materialIndexMap.size());
        }
    }

    return materialIndexMap;
}

std::map<const std::string_view, ResourceBundle::IndexType>
CreateModelIndexMap(const std::span<const ModelDef> modelDefs)
{
    std::map<const std::string_view, ResourceBundle::IndexType> modelIndex;

    const auto view = std::views::zip(modelDefs, std::views::iota(size_t{ 0 }));

    for(const auto& [modelDef, index] : view)
    {
        MLG_ABORTIF(modelDef.Name.empty(), "ModelDef has empty name");
        MLG_ABORTIF(modelIndex.contains(modelDef.Name), "Duplicate model name: {}", modelDef.Name);
        modelIndex[modelDef.Name] = IndexBoundsCheck(index);
    }

    return modelIndex;
}

std::vector<MaterialResource>
CollectMaterials(const std::map<const MaterialDef, ResourceBundle::IndexType>& materialIndexMap,
    const std::map<const std::string_view, ResourceBundle::IndexType>& textureUriIndexMap)
{
    std::vector<MaterialResource> materials;
    materials.resize(materialIndexMap.size());

    for(const auto& [materialDef, materialIndex] : materialIndexMap)
    {
        MLG_ABORTIF(materialIndex >= materials.size(),
            "Material index out of bounds: {}",
            materialIndex);

        ResourceBundle::IndexType baseTextureIndex = ResourceBundle::kInvalidIndex;

        auto it = textureUriIndexMap.find(materialDef.BaseTextureUri);
        if(it != textureUriIndexMap.end())
        {
            baseTextureIndex = it->second;
            MLG_ABORTIF(baseTextureIndex >= textureUriIndexMap.size(),
                "Texture index out of bounds: {}",
                baseTextureIndex);
        }

        const MaterialResource materialResource //
            {
                .BaseTextureIndex = baseTextureIndex,
                .Color = materialDef.Color,
                .Metalness = materialDef.Metalness,
                .Roughness = materialDef.Roughness,
            };

        materials[materialIndex] = materialResource;
    }

    return materials;
}

std::vector<MeshDef>
CollectMeshDefs(const std::span<const ModelDef> modelDefs)
{
    size_t count = 0;
    for(const ModelDef& modelDef : modelDefs)
    {
        count += modelDef.MeshDefs.size();
    }

    std::vector<MeshDef> meshDefs;
    meshDefs.reserve(count);

    for(const ModelDef& modelDef : modelDefs)
    {
        meshDefs.append_range(modelDef.MeshDefs);
    }

    return meshDefs;
}

std::vector<Vertex>
CollectVertices(const std::span<const MeshDef> meshDefs)
{
    size_t count = 0;
    for(const MeshDef& meshDef : meshDefs)
    {
        count += meshDef.Vertices.size();
    }

    std::vector<Vertex> vertices;
    vertices.reserve(count);

    for(const MeshDef& meshDef : meshDefs)
    {
        vertices.append_range(meshDef.Vertices);
    }

    return vertices;
}

std::vector<VertexIndex>
CollectIndices(const std::span<const MeshDef> meshDefs)
{
    size_t count = 0;
    for(const MeshDef& meshDef : meshDefs)
    {
        count += meshDef.Indices.size();
    }

    std::vector<VertexIndex> indices;
    indices.reserve(count);

    for(const MeshDef& meshDef : meshDefs)
    {
        indices.append_range(meshDef.Indices);
    }

    return indices;
}

std::vector<MeshResource>
CollectMeshes(const std::span<const MeshDef> meshDefs,
    const std::map<const MaterialDef, ResourceBundle::IndexType>& materialIndexMap)
{
    std::vector<MeshResource> meshes;
    meshes.reserve(meshDefs.size());

    size_t indexIndex = 0;
    size_t vertexIndex = 0;

    for(const MeshDef& meshDef : meshDefs)
    {
        const size_t indexCount = meshDef.Indices.size();
        const size_t vertexCount = meshDef.Vertices.size();

        const BoundingBox boundingBox =
            BoundingBox::FromVertices(meshDef.Vertices, meshDef.Indices);

        const auto it = materialIndexMap.find(meshDef.MaterialDef);
        MLG_ABORTIF(it == materialIndexMap.end(), "Material not found in material index map");
        const ResourceBundle::IndexType materialIndex = it->second;
        MLG_ABORTIF(materialIndex >= materialIndexMap.size(),
            "Material index out of bounds: {}",
            materialIndex);

        const MeshResource mesh //
            {
                .IndexCount = CountBoundsCheck(indexCount),
                .FirstIndex = IndexBoundsCheck(indexIndex),
                .BaseVertex = IndexBoundsCheck(vertexIndex),
                .MaterialIndex = materialIndex,
                .BoundingBox = boundingBox,
            };
        meshes.push_back(mesh);

        MLG_ABORTIF(kMaxSizeT - indexIndex < indexCount, "Index overflow");
        MLG_ABORTIF(kMaxSizeT - vertexIndex < vertexCount, "Vertex overflow");

        indexIndex += indexCount;
        vertexIndex += vertexCount;
    }

    return meshes;
}

std::vector<ModelResource>
CollectModels(const std::span<const ModelDef> modelDefs, const std::span<const MeshResource> meshes)
{
    std::vector<ModelResource> models;
    models.reserve(modelDefs.size());

    size_t meshIndex = 0;

    // Meshes are collected sequentially for each model.
    // I.e. meshes for model N+1 immediately follow those for model N.

    for(const ModelDef& modelDef : modelDefs)
    {
        const size_t meshCount = modelDef.MeshDefs.size();

        MLG_ABORTIF(meshCount == 0, "Model has no meshes");
        MLG_ABORTIF(meshIndex >= meshes.size(), "Mesh index out of bounds");
        MLG_ABORTIF(meshes.size() - meshIndex < meshCount, "Mesh span out of bounds");

        const std::span meshSpan = meshes.subspan(meshIndex, meshCount);

        BoundingBox boundingBox = meshSpan[0].BoundingBox;
        for(const MeshResource& mesh : meshSpan.subspan(1))
        {
            boundingBox += mesh.BoundingBox;
        }

        const ModelResource model //
            {
                .FirstMeshIndex = IndexBoundsCheck(meshIndex),
                .MeshCount = CountBoundsCheck(meshCount),
                .BoundingBox = boundingBox,
            };
        models.push_back(model);

        meshIndex += meshCount;
    }

    return models;
}

std::vector<ModelInstanceResource>
CollectModelInstances(const std::span<const FlatNodeDef> flatNodeDefs,
    const std::map<const std::string_view, ResourceBundle::IndexType>& modelIndexMap,
    const std::span<const ModelResource> modelResources)
{
    size_t count = 0;
    for(const FlatNodeDef& flatNodeDef : flatNodeDefs)
    {
        const bool hasModel =
            std::visit([&](const auto* nodeDef) { return static_cast<bool>(nodeDef->Model); },
                flatNodeDef.NodeDefPtr);
        if(hasModel)
        {
            ++count;
        }
    }

    std::vector<ModelInstanceResource> modelInstances;
    modelInstances.reserve(count);

    const auto view = std::views::zip(flatNodeDefs, std::views::iota(size_t{ 0 }));

    for(const auto& [flatNodeDef, nodeIndex] : view)
    {
        std::visit(
            [&](const auto* nodeDef)
            {
                const std::optional<ModelRef>& optModel = nodeDef->Model;

                if(optModel)
                {
                    const ModelRef& modelRef = *optModel;
                    auto it = modelIndexMap.find(modelRef.Name);
                    MLG_ABORTIF(it == modelIndexMap.end(), "Model {} not found", modelRef.Name);

                    const ResourceBundle::IndexType modelIdx = it->second;

                    MLG_ABORTIF(modelIdx >= modelResources.size(),
                        "Model index {} out of range",
                        modelIdx);

                    const ModelInstanceResource modelInstance //
                        {
                            .NodeIndex = IndexBoundsCheck(nodeIndex),
                            .ModelIndex = modelIdx,
                        };

                    modelInstances.push_back(modelInstance);
                }
            },
            flatNodeDef.NodeDefPtr);
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

std::vector<ColliderResource>
CollectColliders(const std::span<const RootNodeDef> nodeDefs)
{
    size_t count = 0;
    for(const RootNodeDef& nodeDef : nodeDefs)
    {
        if(nodeDef.Body)
        {
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

        for(const ColliderDef& colliderDef : body->Colliders)
        {
            colliders.push_back(CreateCollider(colliderDef));
        }
    }

    return colliders;
}

std::vector<RigidBodyResource>
CollectRigidBodies(const std::span<const RootNodeDef> nodeDefs,
    const std::span<const ColliderResource> colliders)
{
    size_t count = 0;
    for(const RootNodeDef& nodeDef : nodeDefs)
    {
        if(nodeDef.Body)
        {
            ++count;
        }
    }

    std::vector<RigidBodyResource> rigidBodies;
    rigidBodies.reserve(count);

    size_t colliderIndex = 0;

    const auto view = std::views::zip(nodeDefs, std::views::iota(size_t{ 0 }));

    for(const auto& [nodeDef, nodeIndex] : view)
    {
        const std::optional<RigidBodyDef> body = nodeDef.Body;
        if(!body)
        {
            continue;
        }

        MLG_ABORTIF(body->Colliders.empty(), "Rigid body has no colliders");
        MLG_ABORTIF(colliderIndex >= colliders.size(), "Collider index out of bounds");
        MLG_ABORTIF(colliders.size() - colliderIndex < body->Colliders.size(),
            "Collider span out of bounds");

        const RigidBodyResource rigidBody //
            {
                .NodeIndex = IndexBoundsCheck(nodeIndex),
                .Mass = body->Mass.Value(),
                .MotionType = body->MotionType,
                .FirstColliderIndex = IndexBoundsCheck(colliderIndex),
                .ColliderCount = CountBoundsCheck(body->Colliders.size()),
            };

        rigidBodies.push_back(rigidBody);

        colliderIndex += body->Colliders.size();
    }

    return rigidBodies;
}

std::vector<LevelNodeResource>
CollectLevelNodes(const std::span<const FlatNodeDef> flatNodeDefs)
{
    std::vector<LevelNodeResource> levelNodes;
    levelNodes.reserve(flatNodeDefs.size());

    for(const FlatNodeDef& flatNodeDef : flatNodeDefs)
    {
        std::visit(
            [&](const auto* nodeDef)
            {
                const LevelNodeResource levelNode //
                    {
                        .ParentIndex = flatNodeDef.ParentIndex,
                        .FirstChildIndex = flatNodeDef.FirstChildIndex,
                        .ChildCount = CountBoundsCheck(nodeDef->Children.size()),
                        .LocalPos = nodeDef->Transform.T,
                        .LocalRot = nodeDef->Transform.R.ToVector(),
                        .LocalScale = nodeDef->Transform.S,
                    };

                levelNodes.push_back(levelNode);
            },
            flatNodeDef.NodeDefPtr);
    }

    return levelNodes;
}

template<typename T>
constexpr size_t
SizeOfItem()
{
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "T must have standard layout");

    constexpr size_t size = sizeof(T);
    MLG_ABORTIF(size > ResourceBundle::kMaxOffset, "Maximum item size exceeded");
    return size;
}

template<typename T>
constexpr size_t
SizeOfSpan(const std::span<T>& s)
{
    static constexpr size_t itemSize = SizeOfItem<T>();
    static constexpr size_t kMaxSpan = ResourceBundle::kMaxOffset / itemSize;

    MLG_ABORTIF(s.size() > kMaxSpan, "Maximum span length exceeded");

    return s.size() * itemSize;
}

const ResourceBundle::Header*
GetHeader(const std::vector<char>& buffer)
{
    const void* p = buffer.data();
    return static_cast<const ResourceBundle::Header*>(p);
}

template<typename T>
void
AppendItem(const T& v, std::vector<char>& buffer)
{
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "T must have standard layout");

    const ResourceBundle::Header* header = GetHeader(buffer);

    MLG_ABORTIF(header->TotalSize - buffer.size() < SizeOfItem<T>(), "Not enough space in buffer");

    const void* src = static_cast<const void*>(&v);
    buffer.append_range(std::span(static_cast<const char*>(src), sizeof(T)));
}

template<typename T>
void
AppendSpan(const std::span<const T>& v, std::vector<char>& buffer)
{
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "T must have standard layout");

    const ResourceBundle::Header* header = GetHeader(buffer);

    MLG_ABORTIF(header->TotalSize - buffer.size() < SizeOfSpan(v), "Not enough space in buffer");

    const void* src = static_cast<const void*>(v.data());
    buffer.append_range(std::span(static_cast<const char*>(src), v.size() * sizeof(T)));
}

std::string_view
MakeStringView(const StringResource& resource, const std::span<const char>& chars)
{
    MLG_ABORTIF(resource.CharIndex > chars.size()
            || resource.Length > chars.size() - resource.CharIndex,
        "StringResource out of bounds");

    return std::string_view(chars.subspan(resource.CharIndex, resource.Length));
}

} // namespace

// ResourceBundle

std::string_view
ResourceBundle::GetStringView(const StringResource& stringResource) const
{
    return MakeStringView(stringResource, GetChars());
}

// ResourceBundleBuilder

Result<ResourceBundle>
ResourceBundleBuilder::Build(const LevelDef& levelDef, const PropKitDef& propKitDef)
{
    // MLG_CHECK(Validate(propKitDef, levelDef), "LevelDef validation failed");

    const std::vector<FlatNodeDef> flatNodeDefs = FlattenNodesBreadthFirst(levelDef.NodeDefs);
    const std::vector<MeshDef> meshDefs = CollectMeshDefs(propKitDef.ModelDefs);

    const std::map<const MaterialDef, ResourceBundle::IndexType> materialIndexMap =
        CreateMaterialIndexMap(meshDefs);
    const std::map<const std::string_view, ResourceBundle::IndexType> modelIndexMap =
        CreateModelIndexMap(propKitDef.ModelDefs);

    std::vector<char> chars;
    const std::vector<NodeNameResource> nodeNames = CollectNodeNames(flatNodeDefs, chars);
    const std::vector<StringResource> textureUris = CollectTextureUris(meshDefs, chars);

    std::map<const std::string_view, ResourceBundle::IndexType> textureUriIndexMap;
    for(size_t i = 0; i < textureUris.size(); ++i)
    {
        textureUriIndexMap[MakeStringView(textureUris[i], chars)] = IndexBoundsCheck(i);
    }

    const std::vector<MaterialResource> materials =
        CollectMaterials(materialIndexMap, textureUriIndexMap);
    const std::vector<Vertex> vertices = CollectVertices(meshDefs);
    const std::vector<VertexIndex> indices = CollectIndices(meshDefs);
    const std::vector<MeshResource> meshes = CollectMeshes(meshDefs, materialIndexMap);
    const std::vector<ModelResource> models = CollectModels(propKitDef.ModelDefs, meshes);
    const std::vector<ModelInstanceResource> modelInstances =
        CollectModelInstances(flatNodeDefs, modelIndexMap, models);
    const std::vector<ColliderResource> colliders = CollectColliders(levelDef.NodeDefs);
    const std::vector<RigidBodyResource> rigidBodies =
        CollectRigidBodies(levelDef.NodeDefs, colliders);
    const std::vector<LevelNodeResource> nodes = CollectLevelNodes(flatNodeDefs);

    m_Header = nullptr;
    m_Buffer = {};

#define ADD_AND_CHECK_OVERFLOW(value)                                                              \
    MLG_ABORTIF(ResourceBundle::kMaxOffset - totalSize < (value));                                 \
    totalSize += (value);

    size_t totalSize = SizeOfItem<ResourceBundle::Header>();

    ADD_AND_CHECK_OVERFLOW(SizeOfSpan(std::span(chars)));
    ADD_AND_CHECK_OVERFLOW(SizeOfSpan(std::span(nodeNames)));
    ADD_AND_CHECK_OVERFLOW(SizeOfSpan(std::span(textureUris)));
    ADD_AND_CHECK_OVERFLOW(SizeOfSpan(std::span(materials)));
    ADD_AND_CHECK_OVERFLOW(SizeOfSpan(std::span(vertices)));
    ADD_AND_CHECK_OVERFLOW(SizeOfSpan(std::span(indices)));
    ADD_AND_CHECK_OVERFLOW(SizeOfSpan(std::span(meshes)));
    ADD_AND_CHECK_OVERFLOW(SizeOfSpan(std::span(models)));
    ADD_AND_CHECK_OVERFLOW(SizeOfSpan(std::span(modelInstances)));
    ADD_AND_CHECK_OVERFLOW(SizeOfSpan(std::span(colliders)));
    ADD_AND_CHECK_OVERFLOW(SizeOfSpan(std::span(rigidBodies)));
    ADD_AND_CHECK_OVERFLOW(SizeOfSpan(std::span(nodes)));

#undef ADD_AND_CHECK_OVERFLOW

    MLG_CHECK(totalSize <= ResourceBundle::kMaxBundleSize,
        "Total size exceeds maximum allowed size");

    m_Buffer.reserve(totalSize);

    AppendHeader(static_cast<ResourceBundle::OffsetType>(totalSize));
    Append(chars);
    Append(nodeNames);
    Append(textureUris);
    Append(materials);
    Append(vertices);
    Append(indices);
    Append(meshes);
    Append(models);
    Append(modelInstances);
    Append(colliders);
    Append(rigidBodies);
    Append(nodes);

    m_Header = nullptr;
    return ResourceBundle{ std::move(m_Buffer) };
}

// private:

void
ResourceBundleBuilder::AppendHeader(const ResourceBundle::OffsetType totalSize)
{
    MLG_ASSERT(m_Buffer.empty(), "Header already appended");

    ResourceBundle::Header header{};

    header.TotalSize = totalSize;

    const void* p = &header;
    m_Buffer.append_range(std::span(static_cast<const char*>(p), sizeof(ResourceBundle::Header)));

    void* pp = m_Buffer.data();
    m_Header = static_cast<ResourceBundle::Header*>(pp);
}

void
ResourceBundleBuilder::Append(const std::span<const char>& chars)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->CharsOffset == ResourceBundle::kInvalidOffset, "Chars already appended");

    m_Header->CharsOffset = OffsetBoundsCheck(m_Buffer.size());
    m_Header->CharsLength = CountBoundsCheck(chars.size());
    AppendSpan(chars, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const NodeNameResource>& nodeNames)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->NodeNamesOffset == ResourceBundle::kInvalidOffset,
        "Node names already appended");

    m_Header->NodeNamesOffset = OffsetBoundsCheck(m_Buffer.size());
    m_Header->NodeNameCount = CountBoundsCheck(nodeNames.size());
    AppendSpan(nodeNames, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const StringResource>& textureUris)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->TextureUrisOffset == ResourceBundle::kInvalidOffset,
        "Texture URIs already appended");

    m_Header->TextureUrisOffset = OffsetBoundsCheck(m_Buffer.size());
    m_Header->TextureUriCount = CountBoundsCheck(textureUris.size());
    AppendSpan(textureUris, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const MaterialResource>& materials)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->MaterialsOffset == ResourceBundle::kInvalidOffset,
        "Materials already appended");

    m_Header->MaterialsOffset = OffsetBoundsCheck(m_Buffer.size());
    m_Header->MaterialCount = CountBoundsCheck(materials.size());
    AppendSpan(materials, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const Vertex>& vertices)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->VerticesOffset == ResourceBundle::kInvalidOffset,
        "Vertices already appended");

    m_Header->VerticesOffset = OffsetBoundsCheck(m_Buffer.size());
    m_Header->VertexCount = CountBoundsCheck(vertices.size());
    AppendSpan(vertices, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const VertexIndex>& indices)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->IndicesOffset == ResourceBundle::kInvalidOffset,
        "Indices already appended");

    m_Header->IndicesOffset = OffsetBoundsCheck(m_Buffer.size());
    m_Header->IndexCount = CountBoundsCheck(indices.size());
    AppendSpan(indices, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const MeshResource>& meshes)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->MeshesOffset == ResourceBundle::kInvalidOffset, "Meshes already appended");

    m_Header->MeshesOffset = OffsetBoundsCheck(m_Buffer.size());
    m_Header->MeshCount = CountBoundsCheck(meshes.size());
    AppendSpan(meshes, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const ModelResource>& models)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->ModelsOffset == ResourceBundle::kInvalidOffset, "Models already appended");

    m_Header->ModelsOffset = OffsetBoundsCheck(m_Buffer.size());
    m_Header->ModelCount = CountBoundsCheck(models.size());
    AppendSpan(models, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const ModelInstanceResource>& modelInstances)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->ModelInstancesOffset == ResourceBundle::kInvalidOffset,
        "Model Instances already appended");

    m_Header->ModelInstancesOffset = OffsetBoundsCheck(m_Buffer.size());
    m_Header->ModelInstanceCount = CountBoundsCheck(modelInstances.size());
    AppendSpan(modelInstances, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const ColliderResource>& colliders)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->CollidersOffset == ResourceBundle::kInvalidOffset,
        "Colliders already appended");

    m_Header->CollidersOffset = OffsetBoundsCheck(m_Buffer.size());
    m_Header->ColliderCount = CountBoundsCheck(colliders.size());
    AppendSpan(colliders, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const RigidBodyResource>& rigidBodies)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->RigidBodiesOffset == ResourceBundle::kInvalidOffset,
        "RigidBodies already appended");

    m_Header->RigidBodiesOffset = OffsetBoundsCheck(m_Buffer.size());
    m_Header->RigidBodyCount = CountBoundsCheck(rigidBodies.size());
    AppendSpan(rigidBodies, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const LevelNodeResource>& nodes)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->NodesOffset == ResourceBundle::kInvalidOffset, "Nodes already appended");

    m_Header->NodesOffset = OffsetBoundsCheck(m_Buffer.size());
    m_Header->NodeCount = CountBoundsCheck(nodes.size());
    AppendSpan(nodes, m_Buffer);
}