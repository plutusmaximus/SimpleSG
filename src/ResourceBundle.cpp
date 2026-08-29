#include "ResourceBundle.h"

#include "LevelDefs.h"
#include "shaders/ShaderInterop.h"

#include <limits>
#include <map>
#include <ranges>
#include <string_view>
#include <type_traits>

namespace
{

constexpr uint32_t kInvalidIndex = std::numeric_limits<uint32_t>::max();

using NodeDefPointer = std::variant<const RootNodeDef*, const ChildNodeDef*>;

struct FlatNodeDef
{
    NodeDefPointer NodeDefPtr;
    uint32_t ParentIndex;
    uint32_t FirstChildIndex;
};

[[maybe_unused]] bool
Validate([[maybe_unused]] const PropKitDef& propKitDef, [[maybe_unused]] const LevelDef& levelDef)
{
    return false;
}

std::vector<FlatNodeDef>
FlattenNodesBreadthFirst(const std::span<const RootNodeDef> rootNodeDefs)
{
    struct PendingNode
    {
        NodeDefPointer Node;
        uint32_t ParentIndex;
    };

    std::vector<FlatNodeDef> flatNodes;
    std::vector<PendingNode> pendingNodes;

    // Queue root nodes for processing.
    for(const RootNodeDef& rootNodeDef : rootNodeDefs)
    {
        pendingNodes.emplace_back(&rootNodeDef, kInvalidIndex);
    }

    for(size_t pendingIndex = 0; pendingIndex < pendingNodes.size(); ++pendingIndex)
    {
        const PendingNode& pendingNode = pendingNodes[pendingIndex];

        const uint32_t nodeIndex = static_cast<uint32_t>(flatNodes.size());

        const FlatNodeDef flatNodeDef //
            {
                .NodeDefPtr = pendingNode.Node,
                .ParentIndex = pendingNode.ParentIndex,
                .FirstChildIndex = kInvalidIndex,
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
    uint32_t parentIndex = kInvalidIndex;
    for(FlatNodeDef& flatNode : flatNodes)
    {
        if(flatNode.ParentIndex != parentIndex)
        {
            parentIndex = flatNode.ParentIndex;
            FlatNodeDef& parentNode = flatNodes[parentIndex];
            parentNode.FirstChildIndex = static_cast<uint32_t>(&flatNode - flatNodes.data());
        }
    }

    return flatNodes;
}

std::vector<NodeNameResource>
CollectNodeNames(const std::span<const FlatNodeDef> flatNodeDefs, std::vector<char>& chars)
{
    std::vector<NodeNameResource> nodeNames;
    std::map<const std::string_view, uint32_t> stringIndexMap;

    auto view = std::views::zip(flatNodeDefs, std::views::iota(0u));

    for(const auto& [flatNodeDef, nodeIndex] : view)
    {
        std::visit(
            [&](const auto* nodeDef)
            {
                if(!stringIndexMap.contains(nodeDef->Name))
                {
                    const NodeNameResource nodeName //
                        {
                            .NodeIndex = nodeIndex,
                            .String //
                            {
                                .Offset = static_cast<uint32_t>(chars.size()),
                                .Length = static_cast<uint32_t>(nodeDef->Name.length()),
                            },
                        };

                    stringIndexMap[nodeDef->Name] = static_cast<uint32_t>(nodeNames.size());
                    nodeNames.push_back(nodeName);
                    chars.append_range(nodeDef->Name);
                }
            },
            flatNodeDef.NodeDefPtr);
    }

    return nodeNames;
}

std::vector<TextureUriResource>
CollectTextureUris(const std::span<const MeshDef> meshDefs, std::vector<char>& chars)
{
    std::vector<TextureUriResource> textureUris;
    std::map<const std::string_view, uint32_t> stringIndexMap;

    for(const MeshDef& meshDef : meshDefs)
    {
        const MaterialDef& materialDef = meshDef.MaterialDef;

        if(materialDef.BaseTextureUri.empty())
        {
            continue;
        }

        if(stringIndexMap.contains(materialDef.BaseTextureUri))
        {
            continue;
        }

        const TextureUriResource textureUri //
            {
                .String //
                {
                    .Offset = static_cast<uint32_t>(chars.size()),
                    .Length = static_cast<uint32_t>(materialDef.BaseTextureUri.length()),
                },
            };

        stringIndexMap[materialDef.BaseTextureUri] = static_cast<uint32_t>(textureUris.size());
        textureUris.push_back(textureUri);
        chars.append_range(materialDef.BaseTextureUri);
    }

    return textureUris;
}

std::map<const MaterialDef, uint32_t>
CreateMaterialIndexMap(const std::span<const MeshDef> meshDefs)
{
    std::map<const MaterialDef, uint32_t> materialIndexMap;

    for(const MeshDef& meshDef : meshDefs)
    {
        const MaterialDef& materialDef = meshDef.MaterialDef;
        if(!materialIndexMap.contains(materialDef))
        {
            const uint32_t materialIndex = static_cast<uint32_t>(materialIndexMap.size());
            materialIndexMap[materialDef] = materialIndex;
        }
    }

    return materialIndexMap;
}

std::map<const std::string_view, uint32_t>
CreateModelIndexMap(const std::span<const ModelDef> modelDefs)
{
    std::map<const std::string_view, uint32_t> modelIndex;

    for(size_t i = 0; i < modelDefs.size(); ++i)
    {
        const ModelDef& modelDef = modelDefs[i];
        MLG_ASSERT(!modelDef.Name.empty(), "ModelDef has empty name");
        MLG_ASSERT(!modelIndex.contains(modelDef.Name), "Duplicate model name: {}", modelDef.Name);
        modelIndex[modelDef.Name] = static_cast<uint32_t>(i);
    }

    return modelIndex;
}

std::vector<MaterialResource>
CollectMaterials(const std::map<const MaterialDef, uint32_t>& materialIndexMap,
    const std::map<const std::string_view, uint32_t>& stringIndexMap)
{
    std::vector<MaterialResource> materials;
    materials.reserve(materialIndexMap.size());

    for(const auto& [materialDef, materialIndex] : materialIndexMap)
    {
        uint32_t baseTextureIndex = ResourceBundle::kInvalidIndex;

        auto it = stringIndexMap.find(materialDef.BaseTextureUri);
        if(it != stringIndexMap.end())
        {
            baseTextureIndex = it->second;
        }

        const MaterialResource materialResource //
            {
                .BaseTextureIndex = baseTextureIndex,
                .Color = materialDef.Color,
                .Metalness = materialDef.Metalness,
                .Roughness = materialDef.Roughness,
            };

        materials.push_back(materialResource);
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
    const std::map<const MaterialDef, uint32_t>& materialIndexMap)
{
    std::vector<MeshResource> meshes;
    meshes.reserve(meshDefs.size());

    uint32_t indexOffset = 0;
    uint32_t vertexOffset = 0;

    for(const MeshDef& meshDef : meshDefs)
    {
        const uint32_t indexCount = static_cast<uint32_t>(meshDef.Indices.size());
        const uint32_t vertexCount = static_cast<uint32_t>(meshDef.Vertices.size());

        const BoundingBox boundingBox =
            BoundingBox::FromVertices(meshDef.Vertices, meshDef.Indices);

        const MeshResource mesh //
            {
                .IndexCount = indexCount,
                .FirstIndex = indexOffset,
                .BaseVertex = vertexOffset,
                .MaterialIndex = materialIndexMap.at(meshDef.MaterialDef),
                .BoundingBox = boundingBox,
                .BoundingSphere = BoundingSphere(boundingBox),
            };
        meshes.push_back(mesh);
        indexOffset += indexCount;
        vertexOffset += vertexCount;
    }

    return meshes;
}

std::vector<ModelResource>
CollectModels(const std::span<const ModelDef> modelDefs, const std::span<const MeshResource> meshes)
{
    std::vector<ModelResource> models;
    models.reserve(modelDefs.size());

    uint32_t meshOffset = 0;

    for(const ModelDef& modelDef : modelDefs)
    {
        const uint32_t meshCount = static_cast<uint32_t>(modelDef.MeshDefs.size());
        const std::span meshSpan = meshes.subspan(meshOffset, meshCount);

        BoundingBox boundingBox = meshSpan[0].BoundingBox;
        for(const MeshResource& mesh : meshSpan.subspan(1))
        {
            boundingBox += mesh.BoundingBox;
        }

        const ModelResource model //
            {
                .MeshOffset = meshOffset,
                .MeshCount = meshCount,
                .BoundingBox = boundingBox,
                .BoundingSphere = BoundingSphere(boundingBox),
            };
        models.push_back(model);
        meshOffset += meshCount;
    }

    return models;
}

std::vector<ModelInstanceResource>
CollectModelInstances(const std::span<const FlatNodeDef> flatNodeDefs,
    const std::map<const std::string_view, uint32_t>& modelIndexMap,
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

    uint32_t meshInstanceIndex = 0;

    for(const FlatNodeDef& flatNodeDef : flatNodeDefs)
    {
        const uint32_t nodeIndex = static_cast<uint32_t>(&flatNodeDef - flatNodeDefs.data());

        std::visit(
            [&](const auto* nodeDef)
            {
                const std::optional<ModelRef>& optModel = nodeDef->Model;

                if(optModel)
                {
                    const ModelRef& modelRef = *optModel;
                    auto it = modelIndexMap.find(modelRef.Name);
                    MLG_ASSERT(it != modelIndexMap.end(), "Model {} not found", modelRef.Name);
                    const uint32_t modelIdx = it->second;
                    MLG_ASSERT(modelIdx < modelResources.size(),
                        "Model index {} out of range",
                        modelIdx);
                    const ModelResource* model = &modelResources[modelIdx];

                    const ModelInstanceResource modelInstance //
                        {
                            .NodeIndex = nodeIndex,
                            .ModelIndex = modelIdx,
                            .MeshInstanceOffset = meshInstanceIndex,
                            .MeshInstanceCount = model->MeshCount,
                        };

                    modelInstances.push_back(modelInstance);
                    meshInstanceIndex += model->MeshCount;
                }
            },
            flatNodeDef.NodeDefPtr);
    }

    return modelInstances;
}

std::vector<MeshInstanceResource>
CollectMeshInstances(const std::span<const ModelInstanceResource> modelInstances,
    const std::span<const ModelResource> models)
{
    size_t count = 0;
    for(const ModelInstanceResource& modelInstance : modelInstances)
    {
        const ModelResource* model = &models[modelInstance.ModelIndex];
        count += model->MeshCount;
    }

    std::vector<MeshInstanceResource> meshInstances;
    meshInstances.reserve(count);

    for(const ModelInstanceResource& modelInstance : modelInstances)
    {
        const ModelResource* model = &models[modelInstance.ModelIndex];

        for(uint32_t i = 0; i < model->MeshCount; ++i)
        {
            const MeshInstanceResource meshInstance //
                {
                    .InstanceIndex = static_cast<uint32_t>(meshInstances.size()),
                    .MeshIndex = model->MeshOffset + i,
                };

            meshInstances.push_back(meshInstance);
        }
    }

    return meshInstances;
}

std::vector<ShaderInterop::DrawIndirectParams>
CollectDrawIndirectParams(const std::span<const MeshInstanceResource> meshInstances,
    const std::span<const MeshResource> meshes)
{
    std::vector<ShaderInterop::DrawIndirectParams> drawIndirectParams;
    drawIndirectParams.reserve(meshInstances.size());

    for(const MeshInstanceResource& meshInstance : meshInstances)
    {
        const MeshResource& mesh = meshes[meshInstance.MeshIndex];

        const ShaderInterop::DrawIndirectParams params //
            {
                .IndexCount = mesh.IndexCount,
                .InstanceCount = 1,
                .FirstIndex = mesh.FirstIndex,
                .BaseVertex = mesh.BaseVertex,
                .FirstInstance = static_cast<uint32_t>(drawIndirectParams.size()),
            };

        drawIndirectParams.push_back(params);
    }
    return drawIndirectParams;
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
CollectRigidBodies(const std::span<const RootNodeDef> nodeDefs)
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

    uint32_t colliderOffset = 0;

    for(const RootNodeDef& nodeDef : nodeDefs)
    {
        const std::optional<RigidBodyDef> body = nodeDef.Body;
        if(!body)
        {
            continue;
        }

        const RigidBodyResource rigidBody //
            {
                .NodeIndex = static_cast<uint32_t>(&nodeDef - nodeDefs.data()),
                .Mass = body->Mass.Value(),
                .MotionType = body->MotionType,
                .ColliderOffset = colliderOffset,
                .ColliderCount = static_cast<uint32_t>(body->Colliders.size()),
            };

        rigidBodies.push_back(rigidBody);

        colliderOffset += static_cast<uint32_t>(body->Colliders.size());
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
                        .ChildCount = static_cast<uint32_t>(nodeDef->Children.size()),
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
size_t
SizeOfItem()
{
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "T must have standard layout");

    return sizeof(T);
}

template<typename T>
size_t
SizeOfSpan(const std::span<T>& s)
{
    return s.size() * SizeOfItem<T>();
}

template<typename T>
void
AppendItem(const T& v, std::vector<char>& buffer)
{
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "T must have standard layout");

    MLG_ASSERT(SizeOfItem<T>() + buffer.size() <= buffer.capacity(), "Not enough space in buffer");

    const void* src = static_cast<const void*>(&v);
    buffer.append_range(std::span(static_cast<const char*>(src), sizeof(T)));
}

template<typename T>
void
AppendSpan(const std::span<const T>& v, std::vector<char>& buffer)
{
    static_assert(std::is_trivially_copyable_v<T>, "T must be trivially copyable");
    static_assert(std::is_standard_layout_v<T>, "T must have standard layout");

    MLG_ASSERT(SizeOfSpan(v) + buffer.size() <= buffer.capacity(), "Not enough space in buffer");

    const void* src = static_cast<const void*>(v.data());
    buffer.append_range(std::span(static_cast<const char*>(src), v.size() * sizeof(T)));
}

std::string_view
MakeStringView(const StringResource& resource, const std::span<const char>& chars)
{
    MLG_ASSERT(resource.Offset + resource.Length <= chars.size(), "Resource out of bounds");

    return std::string_view(chars.subspan(resource.Offset, resource.Length));
}

} // namespace

Result<ResourceBundle>
ResourceBundleBuilder::Build(const LevelDef& levelDef, const PropKitDef& propKitDef)
{
    // MLG_CHECK(Validate(propKitDef, levelDef), "LevelDef validation failed");

    const std::vector<FlatNodeDef> flatNodeDefs = FlattenNodesBreadthFirst(levelDef.NodeDefs);
    const std::vector<MeshDef> meshDefs = CollectMeshDefs(propKitDef.ModelDefs);

    const std::map<const MaterialDef, uint32_t> materialIndexMap = CreateMaterialIndexMap(meshDefs);
    const std::map<const std::string_view, uint32_t> modelIndexMap =
        CreateModelIndexMap(propKitDef.ModelDefs);

    std::vector<char> chars;
    const std::vector<NodeNameResource> nodeNames = CollectNodeNames(flatNodeDefs, chars);
    const std::vector<TextureUriResource> textureUris = CollectTextureUris(meshDefs, chars);

    std::map<const std::string_view, uint32_t> textureUriIndexMap;
    for(uint32_t i = 0; i < textureUris.size(); ++i)
    {
        textureUriIndexMap[MakeStringView(textureUris[i].String, chars)] = i;
    }

    const std::vector<MaterialResource> materials =
        CollectMaterials(materialIndexMap, textureUriIndexMap);
    const std::vector<Vertex> vertices = CollectVertices(meshDefs);
    const std::vector<VertexIndex> indices = CollectIndices(meshDefs);
    const std::vector<MeshResource> meshes = CollectMeshes(meshDefs, materialIndexMap);
    const std::vector<ModelResource> models = CollectModels(propKitDef.ModelDefs, meshes);
    const std::vector<ModelInstanceResource> modelInstances =
        CollectModelInstances(flatNodeDefs, modelIndexMap, models);
    const std::vector<MeshInstanceResource> meshInstances =
        CollectMeshInstances(modelInstances, models);
    const std::vector<ShaderInterop::DrawIndirectParams> drawIndirectParams =
        CollectDrawIndirectParams(meshInstances, meshes);
    const std::vector<ColliderResource> colliders = CollectColliders(levelDef.NodeDefs);
    const std::vector<RigidBodyResource> rigidBodies = CollectRigidBodies(levelDef.NodeDefs);
    const std::vector<LevelNodeResource> nodes = CollectLevelNodes(flatNodeDefs);

    m_Header = nullptr;
    m_Buffer = {};

    const uint64_t totalSize = SizeOfItem<ResourceBundle::Header>()
        + SizeOfSpan(std::span(chars))
        + SizeOfSpan(std::span(nodeNames))
        + SizeOfSpan(std::span(textureUris))
        + SizeOfSpan(std::span(materials))
        + SizeOfSpan(std::span(vertices))
        + SizeOfSpan(std::span(indices))
        + SizeOfSpan(std::span(meshes))
        + SizeOfSpan(std::span(models))
        + SizeOfSpan(std::span(meshInstances))
        + SizeOfSpan(std::span(modelInstances))
        + SizeOfSpan(std::span(drawIndirectParams))
        + SizeOfSpan(std::span(colliders))
        + SizeOfSpan(std::span(rigidBodies))
        + SizeOfSpan(std::span(nodes));

    m_Buffer.reserve(totalSize);

    AppendHeader(totalSize);
    Append(chars);
    Append(nodeNames);
    Append(textureUris);
    Append(materials);
    Append(vertices);
    Append(indices);
    Append(meshes);
    Append(models);
    Append(meshInstances);
    Append(modelInstances);
    Append(drawIndirectParams);
    Append(colliders);
    Append(rigidBodies);
    Append(nodes);

    m_Header = nullptr;
    return ResourceBundle{ std::move(m_Buffer) };
}

// private:

void
ResourceBundleBuilder::AppendHeader(const uint64_t totalSize)
{
    MLG_ASSERT(m_Buffer.empty(), "Header already appended");
    AppendItem(ResourceBundle::Header{}, m_Buffer);
    void* p = m_Buffer.data();
    m_Header = static_cast<ResourceBundle::Header*>(p);
    m_Header->TotalSize = totalSize;
}

void
ResourceBundleBuilder::Append(const std::span<const char>& chars)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->CharsOffset == ResourceBundle::kInvalidOffset, "Chars already appended");
    MLG_ASSERT(m_Buffer.size() + chars.size() <= m_Header->TotalSize,
        "Chars span exceeds total size");
    m_Header->CharsOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->CharsLength = static_cast<uint32_t>(chars.size());
    AppendSpan(chars, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const NodeNameResource>& nodeNames)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->NodeNamesOffset == ResourceBundle::kInvalidOffset,
        "Node names already appended");
    MLG_ASSERT(m_Buffer.size() + (nodeNames.size() * sizeof(NodeNameResource))
            <= m_Header->TotalSize,
        "Node names span exceeds total size");
    m_Header->NodeNamesOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->NodeNameCount = static_cast<uint32_t>(nodeNames.size());
    AppendSpan(nodeNames, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const TextureUriResource>& textureUris)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->TextureUrisOffset == ResourceBundle::kInvalidOffset,
        "Texture URIs already appended");
    MLG_ASSERT(m_Buffer.size() + (textureUris.size() * sizeof(TextureUriResource))
            <= m_Header->TotalSize,
        "Texture URIs span exceeds total size");
    m_Header->TextureUrisOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->TextureUriCount = static_cast<uint32_t>(textureUris.size());
    AppendSpan(textureUris, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const MaterialResource>& materials)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->MaterialsOffset == ResourceBundle::kInvalidOffset,
        "Materials already appended");
    MLG_ASSERT(m_Buffer.size() + (materials.size() * sizeof(MaterialResource))
            <= m_Header->TotalSize,
        "Materials span exceeds total size");
    m_Header->MaterialsOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->MaterialCount = static_cast<uint32_t>(materials.size());
    AppendSpan(materials, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const Vertex>& vertices)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->VerticesOffset == ResourceBundle::kInvalidOffset,
        "Vertices already appended");
    MLG_ASSERT(m_Buffer.size() + (vertices.size() * sizeof(Vertex)) <= m_Header->TotalSize,
        "Vertices span exceeds total size");
    m_Header->VerticesOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->VertexCount = static_cast<uint32_t>(vertices.size());
    AppendSpan(vertices, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const VertexIndex>& indices)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->IndicesOffset == ResourceBundle::kInvalidOffset,
        "Indices already appended");
    MLG_ASSERT(m_Buffer.size() + (indices.size() * sizeof(VertexIndex)) <= m_Header->TotalSize,
        "Indices span exceeds total size");
    m_Header->IndicesOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->IndexCount = static_cast<uint32_t>(indices.size());
    AppendSpan(indices, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const MeshResource>& meshes)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->MeshesOffset == ResourceBundle::kInvalidOffset, "Meshes already appended");
    MLG_ASSERT(m_Buffer.size() + (meshes.size() * sizeof(MeshResource)) <= m_Header->TotalSize,
        "Meshes span exceeds total size");
    m_Header->MeshesOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->MeshCount = static_cast<uint32_t>(meshes.size());
    AppendSpan(meshes, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const ModelResource>& models)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->ModelsOffset == ResourceBundle::kInvalidOffset, "Models already appended");
    MLG_ASSERT(m_Buffer.size() + (models.size() * sizeof(ModelResource)) <= m_Header->TotalSize,
        "Models span exceeds total size");
    m_Header->ModelsOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->ModelCount = static_cast<uint32_t>(models.size());
    AppendSpan(models, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const MeshInstanceResource>& meshInstances)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->MeshInstancesOffset == ResourceBundle::kInvalidOffset,
        "Mesh Instances already appended");
    MLG_ASSERT(m_Buffer.size() + (meshInstances.size() * sizeof(MeshInstanceResource))
            <= m_Header->TotalSize,
        "Mesh Instances span exceeds total size");
    m_Header->MeshInstancesOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->MeshInstanceCount = static_cast<uint32_t>(meshInstances.size());
    AppendSpan(meshInstances, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const ModelInstanceResource>& modelInstances)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->ModelInstancesOffset == ResourceBundle::kInvalidOffset,
        "Model Instances already appended");
    MLG_ASSERT(m_Buffer.size() + (modelInstances.size() * sizeof(ModelInstanceResource))
            <= m_Header->TotalSize,
        "Model Instances span exceeds total size");
    m_Header->ModelInstancesOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->ModelInstanceCount = static_cast<uint32_t>(modelInstances.size());
    AppendSpan(modelInstances, m_Buffer);
}

void
ResourceBundleBuilder::Append(
    const std::span<const ShaderInterop::DrawIndirectParams>& drawIndirectParams)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->DrawIndirectParamsOffset == ResourceBundle::kInvalidOffset,
        "DrawIndirectParams already appended");
    MLG_ASSERT(m_Buffer.size()
                + (drawIndirectParams.size() * sizeof(ShaderInterop::DrawIndirectParams))
            <= m_Header->TotalSize,
        "DrawIndirectParams span exceeds total size");
    m_Header->DrawIndirectParamsOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->DrawIndirectParamsCount = static_cast<uint32_t>(drawIndirectParams.size());
    AppendSpan(drawIndirectParams, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const ColliderResource>& colliders)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->CollidersOffset == ResourceBundle::kInvalidOffset,
        "Colliders already appended");
    MLG_ASSERT(m_Buffer.size() + (colliders.size() * sizeof(ColliderResource))
            <= m_Header->TotalSize,
        "Colliders span exceeds total size");
    m_Header->CollidersOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->ColliderCount = static_cast<uint32_t>(colliders.size());
    AppendSpan(colliders, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const RigidBodyResource>& rigidBodies)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->RigidBodiesOffset == ResourceBundle::kInvalidOffset,
        "RigidBodies already appended");
    MLG_ASSERT(m_Buffer.size() + (rigidBodies.size() * sizeof(RigidBodyResource))
            <= m_Header->TotalSize,
        "RigidBodies span exceeds total size");
    m_Header->RigidBodiesOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->RigidBodyCount = static_cast<uint32_t>(rigidBodies.size());
    AppendSpan(rigidBodies, m_Buffer);
}

void
ResourceBundleBuilder::Append(const std::span<const LevelNodeResource>& nodes)
{
    MLG_ASSERT(m_Header != nullptr, "Header is not initialized");
    MLG_ASSERT(m_Header->NodesOffset == ResourceBundle::kInvalidOffset, "Nodes already appended");
    MLG_ASSERT(m_Buffer.size() + (nodes.size() * sizeof(LevelNodeResource)) <= m_Header->TotalSize,
        "Nodes span exceeds total size");
    m_Header->NodesOffset = static_cast<uint32_t>(m_Buffer.size());
    m_Header->NodeCount = static_cast<uint32_t>(nodes.size());
    AppendSpan(nodes, m_Buffer);
}