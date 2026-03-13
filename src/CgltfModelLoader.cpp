#define _CRT_SECURE_NO_WARNINGS

#define __LOGGER_NAME__ "GLTF"

#include "CgltfModelLoader.h"

#include "FileFetcher.h"
#include "Log.h"
#include "ThreadPool.h"
#include "Vertex.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <stb_image.h>

#include <atomic>
#include <filesystem>
#include <map>
#include <vector>

namespace
{
struct PrimitiveAttributes
{
    const cgltf_mesh* Mesh{nullptr};
    const cgltf_primitive* Primitive{nullptr};
    const cgltf_material* Material{nullptr};
    uint32_t IndexInMesh{0};
    const cgltf_accessor* Indices{nullptr};
    const cgltf_accessor* Position{nullptr};
    const cgltf_accessor* Normal{nullptr};
    const cgltf_accessor* Texcoord0{nullptr};
    const cgltf_accessor* Texcoord1{nullptr};

    const char* GetMeshName() const { return Mesh->name ? Mesh->name : "<unnamed>"; }
};

struct Node
{
    size_t ParentIndex;
    const cgltf_node* NodePtr;
};

struct PrimitiveInstance
{
    size_t NodeIndex;
    size_t PrimitiveIndex;
};

struct MeshDrawParams
{
    uint32_t IndexCount;
    uint32_t InstanceCount;
    uint32_t FirstIndex;
    uint32_t BaseVertex;
    uint32_t FirstInstance;
};

struct Material
{

};

struct SceneData
{
    std::vector<PrimitiveAttributes> PrimitiveAttrs;
    std::vector<Node> Nodes;
    std::vector<PrimitiveInstance> PrimitiveInstances;
    std::map<const cgltf_primitive*, size_t> PrimitiveIndices;

    std::vector<VertexIndex> Indices;
    std::vector<Vertex> Vertices;
    std::vector<Mat44f> Transforms;
    std::vector<MeshDrawParams> MeshDrawParams;
    std::vector<uint32_t> MeshToTransformMap;
};
}

static Result<PrimitiveAttributes>
CollectPrimitiveAttributes(const cgltf_primitive& primitive)
{
    if(primitive.type != cgltf_primitive_type_triangles)
    {
        MLG_WARN("Only triangle primitives are supported. Ignoring.");
        return Result<>::Fail;
    }

    if(!primitive.material)
    {
        MLG_WARN("Primitive does not have a material. Ignoring.");
        return Result<>::Fail;
    }

    if(primitive.attributes_count == 0)
    {
        MLG_WARN("Primitive does not have any attributes. Ignoring.");
        return Result<>::Fail;
    }

    if(primitive.targets_count != 0)
    {
        MLG_WARN("Morph targets are not supported. Ignoring.");
        return Result<>::Fail;
    }

    if(primitive.has_draco_mesh_compression)
    {
        MLG_WARN("Draco mesh compression is not supported. Ignoring.");
        return Result<>::Fail;
    }

    PrimitiveAttributes attrs//
    {
        .Primitive = &primitive,
        .Material = primitive.material,
        .Indices = primitive.indices,
    };

    for(cgltf_size i = 0; i < primitive.attributes_count; ++i)
    {
        cgltf_attribute& attribute = primitive.attributes[i];

        MLG_LOG_SCOPE("attr {}/{}", attribute.name ? attribute.name : "<unnamed>", i);

        if(attribute.data->is_sparse)
        {
            MLG_WARN("Sparse attribute data is not supported. Primitive will be ignored");
            return Result<>::Fail;
        }

        switch(attribute.type)
        {
            case cgltf_attribute_type_position:
                attrs.Position = attribute.data;
                break;
            case cgltf_attribute_type_normal:
                attrs.Normal = attribute.data;
                break;
            case cgltf_attribute_type_texcoord:
                if(0 == attribute.index)
                {
                    attrs.Texcoord0 = attribute.data;
                }
                else if(1 == attribute.index)
                {
                    attrs.Texcoord1 = attribute.data;
                }
                else
                {
                    /*Log::Warn(
                        "Unsupported texcoord index {} will be ignored",
                        attribute.index);*/
                }
                break;
            default:
                /*Log::Warn(
                    "Unsupported attribute type {} will be ignored",
                    std::to_underlying(attribute.type));*/
                break;
        }
    }

    if(!attrs.Position || attrs.Position->count == 0)
    {
        MLG_WARN("Primitive does not have a POSITION attribute.  Ignoring.");
        return Result<>::Fail;
    }

    const size_t posCount = attrs.Position ? attrs.Position->count : 0;
    const size_t normalCount = attrs.Normal ? attrs.Normal->count : posCount;
    const size_t texcoord0Count = attrs.Texcoord0 ? attrs.Texcoord0->count : posCount;
    const size_t texcoord1Count = attrs.Texcoord1 ? attrs.Texcoord1->count : posCount;

    MLG_CHECK(normalCount == posCount,
        "Normal count {} does not match position count {}",
        normalCount,
        posCount);
    MLG_CHECK(texcoord0Count == posCount,
        "Texcoord0 count {} does not match position count {}",
        texcoord0Count,
        posCount);
    MLG_CHECK(texcoord1Count == posCount,
        "Texcoord1 count {} does not match position count {}",
        texcoord1Count,
        posCount);

    return attrs;
}

// Collect nodes.
static Result<>
CollectNodes(cgltf_node** const childNodes,
    const cgltf_size nodeCount,
    const size_t parentIndex,
    SceneData& sceneData)
{
    for(cgltf_size i = 0; i < nodeCount; ++i)
    {
        const cgltf_node* srcNode = childNodes[i];

        MLG_LOG_SCOPE("node {}", srcNode->name ? srcNode->name : "<unnamed>");

        const size_t nodeIndex = sceneData.Nodes.size();

        CollectNodes(srcNode->children, srcNode->children_count, nodeIndex, sceneData);

        if(sceneData.Nodes.size() == nodeIndex && !srcNode->mesh)
        {
            // Node has no mesh and no descendents with meshes, skip it
            // This could be a procedurally generated mesh, e.g. the leaves of a tree.
            // In blender the following steps could be used to convert to a mesh:
            // 1. Select the node in the outliner.
            // 2. In the "Layout" editor select the "Object" menu and choose "Apply -> Make Instances Real".
            //      This will generate meshes and possibly mesh instances.
            // 3. In the outliner select all the resulting objects.
            // 4. In the "Layout" editor select the "Object" menu and choose "Convert -> Mesh".
            //      This will materialize instances into stand alone meshes.
            // 5. In the outliner select all the resulting objects (meshes).
            // 6. In the "Layout" editor select the "Object" menu and choose "Join".
            //      This will join all the selected meshes into a single mesh.

            const char* type;
            if(srcNode->camera)
            {
                type = "camera";
            }
            else if(srcNode->light)
            {
                type = "light";
            }
            else if(srcNode->skin)
            {
                type = "skin";
            }
            else if(srcNode->weights)
            {
                type = "weights";
            }
            else
            {
                type = "unknown";
            }

            MLG_WARN("{} node has no mesh and no children.  Ignoring.", type);
            continue;
        }

        // Collect primitive instances that are children of this node.

        if(srcNode->mesh)
        {
            const size_t primCount = srcNode->mesh ? srcNode->mesh->primitives_count : 0;

            MLG_LOG_SCOPE("mesh {}/{}", srcNode->mesh->name ? srcNode->mesh->name : "<unnamed>", i);

            for(cgltf_size j = 0; j < primCount; ++j)
            {
                MLG_LOG_SCOPE("prim {}", j);

                const cgltf_primitive* prim = &srcNode->mesh->primitives[j];

                if(!prim->material)
                {
                    MLG_WARN("Primitive has no material.  Ignoring");
                    continue;
                }

                MLG_LOG_SCOPE("mtl {}", prim->material->name ? prim->material->name : "<unnamed>");

                if(!prim->material->has_pbr_metallic_roughness)
                {
                    MLG_WARN("Primitive does not have PBR metallic-roughness material.  Ignoring.");
                    continue;
                }

                // Is this an instance of a primitive that was already collected?
                if(!sceneData.PrimitiveIndices.contains(prim))
                {
                    Result<PrimitiveAttributes> attrs = CollectPrimitiveAttributes(*prim);
                    if(!attrs)
                    {
                        continue;
                    }

                    attrs->Mesh = srcNode->mesh;
                    attrs->IndexInMesh = static_cast<uint32_t>(j);
                    sceneData.PrimitiveIndices[prim] = sceneData.PrimitiveAttrs.size();
                    sceneData.PrimitiveAttrs.push_back(*attrs);
                }

                const PrimitiveInstance primInstance //
                    {
                        .NodeIndex = sceneData.Nodes.size(),
                        .PrimitiveIndex = sceneData.PrimitiveIndices[prim],
                    };

                sceneData.PrimitiveInstances.emplace_back(primInstance);
            }
        }

        Node node//
        {
            .ParentIndex = parentIndex,
            .NodePtr = srcNode,
        };

        sceneData.Nodes.emplace_back(node);
    }

    return Result<>::Ok;
}

static Result<>
CollectMaterials(std::filesystem::path basePath, SceneData& sceneData)
{
    std::map<std::string, FileFetcher::Request> fetchRequests;

    for(const auto&[prim, index] : sceneData.PrimitiveIndices)
    {
        const PrimitiveAttributes& attrs = sceneData.PrimitiveAttrs[index];

        MLG_LOG_SCOPE("mesh {}", attrs.GetMeshName());
        MLG_LOG_SCOPE("prim {}", attrs.IndexInMesh);

        if(!attrs.Material->pbr_metallic_roughness.base_color_texture.texture)
        {
            MLG_WARN("Primitive has no base color texture");
            continue;
        }

        if(!attrs.Material->pbr_metallic_roughness.base_color_texture.texture->image)
        {
            MLG_WARN("Primitive has no base color texture image");
            continue;
        }

        if(!attrs.Material->pbr_metallic_roughness.base_color_texture.texture->image->uri)
        {
            MLG_WARN("Primitive has no base color texture image URI");
            continue;
        }

        const std::string baseTexUri =
            attrs.Material->pbr_metallic_roughness.base_color_texture.texture->image->uri;

        std::string metallicRoughnessTexUri;

        if(attrs.Material->pbr_metallic_roughness.metallic_roughness_texture.texture
            && attrs.Material->pbr_metallic_roughness.metallic_roughness_texture.texture->image
            && attrs.Material->pbr_metallic_roughness.metallic_roughness_texture.texture->image->uri)
        {
            metallicRoughnessTexUri =
                attrs.Material->pbr_metallic_roughness.metallic_roughness_texture.texture->image->uri;
        }

        MLG_LOG_SCOPE("baseTex {}", baseTexUri);
        MLG_LOG_SCOPE("mrTex {}", metallicRoughnessTexUri.empty() ? "none" : metallicRoughnessTexUri);

        MLG_DEBUG("Loading texture files...");

        auto [btIt, btInserted] =
            fetchRequests.emplace(baseTexUri, (basePath / baseTexUri).string());

        if(btInserted)
        {
            if(!FileFetcher::Fetch(btIt->second))
            {
                MLG_WARN("Failed to fetch texture: {}", btIt->second.FilePath);
            }
        }

        if(!metallicRoughnessTexUri.empty())
        {
            auto [mrIt, mrInserted] = fetchRequests.emplace(metallicRoughnessTexUri,
                (basePath / metallicRoughnessTexUri).string());

            if(mrInserted)
            {
                if(!FileFetcher::Fetch(mrIt->second))
                {
                    MLG_WARN("Failed to fetch texture: {}", mrIt->second.FilePath);
                }
            }
        }
    }

    bool pending;

    do
    {
        pending = false;

        FileFetcher::ProcessCompletions();

        for(auto& [name, request] : fetchRequests)
        {
            if(request.IsPending())
            {
                pending = true;
                break;
            }
        }
    }while (pending);

    MLG_DEBUG("Loaded {} textures", fetchRequests.size());

    std::vector<std::atomic<bool>> decodesComplete(fetchRequests.size());

    size_t decodeCompleteIdx = 0;

    for(auto& [name, request] : fetchRequests)
    {
        auto decode = [&name, &request, &decodesComplete, decodeCompleteIdx]()
        {
            MLG_LOG_SCOPE("{}", name);

            MLG_DEBUG("Decoding...");

            int width, height, numChannels;
            stbi_uc* data = stbi_load_from_memory(request.Data.data(), static_cast<int>(request.Data.size()), &width, &height, &numChannels, 0);
            if(!data)
            {
                MLG_ERROR("Error decoding - {}", stbi_failure_reason());
            }
            else
            {
                stbi_image_free(data);
            }

            decodesComplete[decodeCompleteIdx] = true;
        };

        ++decodeCompleteIdx;

        ThreadPool::Enqueue(decode);
    }

    do
    {
        pending = false;

        for(auto& completed : decodesComplete)
        {
            if(!completed)
            {
                pending = true;
                break;
            }
        }
    }while (pending);

    return Result<>::Ok;
}

static Result<>
BuildVertexBuffer(SceneData& sceneData)
{
    // Compute size of vertex buffer.
    size_t vertexCount = 0;
    for(const PrimitiveAttributes& attrs : sceneData.PrimitiveAttrs)
    {
        vertexCount += attrs.Position->count;
    }

    sceneData.Vertices.resize(vertexCount);

    Vertex* vtxDst = reinterpret_cast<Vertex*>(sceneData.Vertices.data());

    for(PrimitiveAttributes& attrs : sceneData.PrimitiveAttrs)
    {
        MLG_LOG_SCOPE("mesh {}", attrs.GetMeshName());
        MLG_LOG_SCOPE("prim {}", attrs.IndexInMesh);

        for(cgltf_size i = 0; i < attrs.Position->count; ++i, ++vtxDst)
        {
            MLG_CHECK(cgltf_accessor_read_float(attrs.Position, i, &vtxDst->pos.x, 3),
                "Failed to read POSITION attribute");

            // Convert from right handed to left handed.
            vtxDst->pos.z = -vtxDst->pos.z;

            if(attrs.Normal)
            {
                MLG_CHECK(cgltf_accessor_read_float(attrs.Normal, i, &vtxDst->normal.x, 3),
                    "Failed to read NORMAL attribute");
            }

            if(attrs.Texcoord0)
            {
                MLG_CHECK(cgltf_accessor_read_float(attrs.Texcoord0, i, &vtxDst->uvs[0].u, 2),
                    "Failed to read TEXCOORD_0 attribute");
            }
            else
            {
                vtxDst->uvs[0].u = 0.0f;
                vtxDst->uvs[0].v = 0.0f;
            }

            if(attrs.Texcoord1)
            {
                MLG_CHECK(cgltf_accessor_read_float(attrs.Texcoord1, i, &vtxDst->uvs[1].u, 2),
                    "Failed to read TEXCOORD_1 attribute");
            }
            else
            {
                vtxDst->uvs[1].u = 0.0f;
                vtxDst->uvs[1].v = 0.0f;
            }
        }
    }

    return Result<>::Ok;
}

static Result<>
BuildIndexBuffer(SceneData& sceneData)
{
    // Compute size of index buffer.
    size_t indexCount = 0;
    for(const PrimitiveAttributes& attrs : sceneData.PrimitiveAttrs)
    {
        if(attrs.Indices)
        {
            indexCount += attrs.Indices->count;
        }
        else
        {
            // Non-indexed primitive.
            indexCount += attrs.Position->count;
        }
    }

    sceneData.Indices.resize(indexCount);

    uint32_t* idxDst = reinterpret_cast<uint32_t*>(sceneData.Indices.data());

    for(PrimitiveAttributes& attrs : sceneData.PrimitiveAttrs)
    {
        MLG_LOG_SCOPE("mesh {}", attrs.GetMeshName());
        MLG_LOG_SCOPE("prim {}", attrs.IndexInMesh);

        size_t idxCount = 0;

        if(attrs.Indices)
        {
            idxCount = attrs.Indices->count;

            const size_t count = cgltf_accessor_unpack_indices(attrs.Indices,
                idxDst,
                sizeof(uint32_t),
                idxCount);

            MLG_CHECK(count == idxCount,
                "Failed to unpack all indices for primitive");
        }
        else
        {
            idxCount = attrs.Position->count;
            for(size_t i = 0; i < idxCount; ++i)
            {
                idxDst[i] = static_cast<uint32_t>(i);
            }
        }

        idxDst += idxCount;
    }

    // Change winding from CCW to CW.

    for(size_t i = 0; i < indexCount; i += 3)
    {
        std::swap(sceneData.Indices[i + 1], sceneData.Indices[i + 2]);
    }

    return Result<>::Ok;
}

static Result<>
GenerateNormals(SceneData& sceneData)
{
    Vertex* vtx = sceneData.Vertices.data();
    const VertexIndex* idx = sceneData.Indices.data();

    for(const auto& attrs : sceneData.PrimitiveAttrs)
    {
        MLG_LOG_SCOPE("mesh {}", attrs.GetMeshName());
        MLG_LOG_SCOPE("prim {}", attrs.IndexInMesh);

        const size_t idxCount = attrs.Indices ? attrs.Indices->count : attrs.Position->count;

        if(!attrs.Normal)
        {
            for(size_t i = 0; i < attrs.Position->count; ++i)
            {
                vtx[i].normal = {0.0f, 0.0f, 0.0f};
            }

            for(size_t i = 0; i < idxCount; i += 3)
            {
                MLG_LOG_SCOPE("tri {}", i / 3);

                Vertex& v0 = vtx[idx[i + 0]];
                Vertex& v1 = vtx[idx[i + 1]];
                Vertex& v2 = vtx[idx[i + 2]];

                const Vec3f normal0 = (v2.pos - v0.pos).Cross(v1.pos - v0.pos).Normalize();
                const Vec3f normal1 = (v0.pos - v1.pos).Cross(v2.pos - v1.pos).Normalize();
                const Vec3f normal2 = (v1.pos - v2.pos).Cross(v0.pos - v2.pos).Normalize();

                v0.normal += normal0;
                v1.normal += normal1;
                v2.normal += normal2;
            }

            for(size_t i = 0; i < attrs.Position->count; ++i)
            {
                vtx[i].normal = vtx[i].normal.Normalize();
            }
        }
    }

    return Result<>::Ok;
}

static
void ConvertRHtoLH(Mat44f& M)
{
    // negate Z components of X and Y basis vectors
    M.m[0].z = -M.m[0].z;
    M.m[1].z = -M.m[1].z;

    // negate X,Y components of Z basis vector
    M.m[2].x = -M.m[2].x;
    M.m[2].y = -M.m[2].y;

    // negate translation Z
    M.m[3].z = -M.m[3].z;
}

static Result<>
BuildTransformBuffer(SceneData& sceneData)
{
    sceneData.Transforms.resize(sceneData.Nodes.size());

    Mat44f* dst = sceneData.Transforms.data();

    for(const Node& node : sceneData.Nodes)
    {
        cgltf_node_transform_local(node.NodePtr, &dst->m[0].x);
        ConvertRHtoLH(*dst);
        ++dst;
    }

    return Result<>::Ok;
}

static Result<>
BuildMeshBuffers(SceneData& sceneData)
{
    sceneData.MeshDrawParams.resize(sceneData.PrimitiveInstances.size());
    sceneData.MeshToTransformMap.resize(sceneData.PrimitiveInstances.size());

    uint32_t firstIndex = 0;
    uint32_t baseVertex = 0;

    for(size_t i = 0; i < sceneData.PrimitiveInstances.size(); ++i)
    {
        const PrimitiveInstance& instance = sceneData.PrimitiveInstances[i];
        const PrimitiveAttributes& attrs = sceneData.PrimitiveAttrs[instance.PrimitiveIndex];

        sceneData.MeshToTransformMap[i] = static_cast<uint32_t>(instance.NodeIndex);

        MeshDrawParams& params = sceneData.MeshDrawParams[i];

        const size_t indexCount = attrs.Indices ? attrs.Indices->count : attrs.Position->count;

        params.IndexCount = static_cast<uint32_t>(indexCount);
        params.InstanceCount = 1;
        params.FirstIndex = firstIndex;
        params.BaseVertex = baseVertex;
        params.FirstInstance = static_cast<uint32_t>(i);

        firstIndex += params.IndexCount;
        baseVertex += static_cast<uint32_t>(attrs.Position->count);
    }
    return Result<>::Ok;
}

Result<>
CgltfModelLoader::LoadModel(const std::string& path)
{
    std::filesystem::path filePath(path);

    MLG_LOG_SCOPE(filePath.filename().string());

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    MLG_CHECK(result == cgltf_result_success, "Failed to load glTF file");

    MLG_CHECK(data->scenes_count > 0, "No scenes found");
    MLG_CHECK(data->scenes_count == 1,
        "Multiple scenes found, only one scene is supported");

    for(cgltf_size i = 0; i < data->buffer_views_count; ++i)
    {
        const cgltf_buffer_view& buffer_view = data->buffer_views[i];
        MLG_CHECK(!buffer_view.has_meshopt_compression,
            "Unsupported meshopt compression in buffer view {}",
            i);
    }

    SceneData sceneData;
    MLG_CHECK(CollectNodes(data->scenes[0].nodes, data->scenes[0].nodes_count, SIZE_MAX, sceneData));

    cgltf_result loadBuffersResult = cgltf_load_buffers(&options, data, filePath.string().c_str());
    MLG_CHECK(loadBuffersResult == cgltf_result_success, "Failed to load buffers");

    MLG_CHECK(BuildVertexBuffer(sceneData));
    MLG_CHECK(BuildIndexBuffer(sceneData));
    MLG_CHECK(GenerateNormals(sceneData));
    MLG_CHECK(BuildTransformBuffer(sceneData));
    MLG_CHECK(BuildMeshBuffers(sceneData));
    MLG_CHECK(CollectMaterials(filePath.parent_path(), sceneData));

    cgltf_free(data);

    return Result<>::Ok;
}