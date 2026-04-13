#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#define __LOGGER_NAME__ "GLTF"

#include "CgltfModelLoader.h"

#include "Log.h"
#include "Vertex.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#include <filesystem>
#include <map>
#include <vector>

namespace
{
struct CgltfPrimitiveAttributes
{
    const cgltf_accessor* SrcIndices{ nullptr };
    const cgltf_accessor* SrcPosition{ nullptr };
    const cgltf_accessor* SrcNormal{ nullptr };
    const cgltf_accessor* SrcTexcoord0{ nullptr };
    const cgltf_accessor* SrcTexcoord1{ nullptr };
};

struct CgltfPrimitiveData
{
    const cgltf_primitive* Primitive;
    CgltfPrimitiveAttributes Attributes;
};

struct CgltfMeshData
{
    const cgltf_mesh* Mesh;
    std::vector<CgltfPrimitiveData> Primitives;
};

struct ModelData
{
    uint32_t FirstMesh;
    uint32_t MeshCount;
};

} // namespace

template<typename T, typename U>
static T narrow_cast(U u)
{
    static_assert(std::is_integral_v<T> && std::is_integral_v<U>, "narrow_cast requires integral types");
    static_assert((std::numeric_limits<T>::is_signed == std::numeric_limits<U>::is_signed) ||
                      (std::numeric_limits<T>::is_signed && !std::numeric_limits<U>::is_signed),
        "narrow_cast requires both types to have the same signedness, or the destination type to be signed and the source type to be unsigned");
    static_assert(std::numeric_limits<T>::digits <= std::numeric_limits<U>::digits,
        "narrow_cast requires the destination type to have fewer or the same digits than the source type");
    const T t = static_cast<T>(u);
    MLG_ASSERT(static_cast<U>(t) == u, "narrow_cast failed: {} -> {}", u, t);
    return t;
}

static Result<CgltfPrimitiveAttributes>
CollectAttributes(const cgltf_primitive& primitive)
{
    MLG_CHECK(primitive.type == cgltf_primitive_type_triangles,
        "Only triangle primitives are supported.");

    MLG_CHECK(primitive.material, "Primitive does not have a material. Ignoring.");

    MLG_CHECK(primitive.attributes_count > 0, "Primitive does not have any attributes. Ignoring.");

    MLG_CHECK(primitive.targets_count == 0, "Morph targets are not supported. Ignoring.");

    MLG_CHECK(!primitive.has_draco_mesh_compression,
        "Draco mesh compression is not supported. Ignoring.");

    CgltfPrimitiveAttributes attrs //
    {
        .SrcIndices = primitive.indices,
    };

    for(cgltf_size i = 0; i < primitive.attributes_count; ++i)
    {
        cgltf_attribute& attribute = primitive.attributes[i];

        MLG_LOG_SCOPE("attr {}/{}", attribute.name ? attribute.name : "<unnamed>", i);

        MLG_CHECK(!attribute.data->is_sparse,
            "Sparse attribute data is not supported. Primitive will be ignored");

        switch(attribute.type)
        {
            case cgltf_attribute_type_position:
                attrs.SrcPosition = attribute.data;
                break;
            case cgltf_attribute_type_normal:
                attrs.SrcNormal = attribute.data;
                break;
            case cgltf_attribute_type_texcoord:
                if(0 == attribute.index)
                {
                    attrs.SrcTexcoord0 = attribute.data;
                }
                else if(1 == attribute.index)
                {
                    attrs.SrcTexcoord1 = attribute.data;
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

    MLG_CHECK(attrs.SrcPosition && attrs.SrcPosition->count > 0,
        "Primitive does not have a POSITION attribute.  Ignoring.");

    const size_t posCount = attrs.SrcPosition ? attrs.SrcPosition->count : 0;
    const size_t normalCount = attrs.SrcNormal ? attrs.SrcNormal->count : posCount;
    const size_t texcoord0Count = attrs.SrcTexcoord0 ? attrs.SrcTexcoord0->count : posCount;
    const size_t texcoord1Count = attrs.SrcTexcoord1 ? attrs.SrcTexcoord1->count : posCount;

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

static Result<>
CollectMeshes(const cgltf_data* gltfData, std::vector<CgltfMeshData>& gltfMeshes)
{
    gltfMeshes.clear();

    for(cgltf_size i = 0; i < gltfData->meshes_count; ++i)
    {
        const cgltf_mesh* mesh = &gltfData->meshes[i];

        MLG_LOG_SCOPE("mesh {}/{}", mesh->name ? mesh->name : "<unnamed>", i);

        CgltfMeshData meshData//
        {
            .Mesh = mesh,
        };

        for(cgltf_size j = 0; j < mesh->primitives_count; ++j)
        {
            MLG_LOG_SCOPE("prim {}", j);

            const cgltf_primitive& prim = mesh->primitives[j];

            if(!prim.material)
            {
                MLG_WARN("Primitive has no material.  Ignoring");
                continue;
            }

            auto attrs = CollectAttributes(prim);

            if(!attrs)
            {
                continue;
            }

            CgltfPrimitiveData primData //
            {
                .Primitive = &prim,
                .Attributes = *attrs,
            };

            meshData.Primitives.emplace_back(std::move(primData));
        }

        gltfMeshes.emplace_back(std::move(meshData));
    }

    return Result<>::Ok;
}

static Result<MaterialData>
CreateMaterialData(const cgltf_material* gltfMaterial)
{
    std::string baseTextureUri;
    RgbaColorf color{"#FF00FFFF"_rgba};
    float metalness = 0;
    float roughness = 0;

    if(gltfMaterial->has_pbr_metallic_roughness)
    {
        const cgltf_pbr_metallic_roughness& pbr = gltfMaterial->pbr_metallic_roughness;
        color = RgbaColorf //
            {
                pbr.base_color_factor[0],
                pbr.base_color_factor[1],
                pbr.base_color_factor[2],
                pbr.base_color_factor[3],
            };
        metalness = pbr.metallic_factor;
        roughness = pbr.roughness_factor;

        if(!pbr.base_color_texture.texture)
        {
            MLG_WARN("Primitive has no base color texture");
        }
        else if(!pbr.base_color_texture.texture->image)
        {
            MLG_WARN("Primitive has no base color texture image");
        }
        else if(!pbr.base_color_texture.texture->image->uri)
        {
            MLG_WARN("Primitive has no base color texture image URI");
        }
        else
        {
            baseTextureUri = pbr.base_color_texture.texture->image->uri;
        }
    }
    else
    {
        MLG_WARN("Primitive has no PBR metallic-roughness material");
    }

    MaterialData materialData //
    {
        .BaseTextureUri = baseTextureUri,
        .Color = color,
        .Metalness = metalness,
        .Roughness = roughness,
    };

    return std::move(materialData);
}

static Result<size_t>
CollectVertices(const CgltfPrimitiveAttributes& attrs, std::vector<Vertex>& vertices)
{
    // GLTF uses a rignt handed coordinate system.  +Y up, +Z forward, -X right.
    // We use left handed.  +Y up, +Z forward, +X right.
    for(cgltf_size i = 0; i < attrs.SrcPosition->count; ++i)
    {
        Vertex vertex;
        MLG_CHECK(cgltf_accessor_read_float(attrs.SrcPosition, i, &vertex.pos.x, 3),
            "Failed to read POSITION attribute");

        // Convert from right handed to left handed.
        vertex.pos.x = -vertex.pos.x;

        if(attrs.SrcNormal)
        {
            MLG_CHECK(cgltf_accessor_read_float(attrs.SrcNormal, i, &vertex.normal.x, 3),
                "Failed to read NORMAL attribute");

            // Convert from right handed to left handed.
            vertex.normal.x = -vertex.normal.x;
        }

        if(attrs.SrcTexcoord0)
        {
            MLG_CHECK(cgltf_accessor_read_float(attrs.SrcTexcoord0, i, &vertex.uvs[0].u, 2),
                "Failed to read TEXCOORD_0 attribute");
        }
        else
        {
            vertex.uvs[0].u = 0.0f;
            vertex.uvs[0].v = 0.0f;
        }

        /*if(attrs.SrcTexcoord1)
        {
            MLG_CHECK(cgltf_accessor_read_float(attrs.SrcTexcoord1, i, &vertex.uvs[1].u, 2),
                "Failed to read TEXCOORD_1 attribute");
        }
        else
        {
            vertex.uvs[1].u = 0.0f;
            vertex.uvs[1].v = 0.0f;
        }*/

        vertices.push_back(vertex);
    }

    return attrs.SrcPosition->count;
}

static Result<size_t>
CollectIndices(const CgltfPrimitiveAttributes& attrs, std::vector<VertexIndex>& indices)
{
    const size_t startIndex = indices.size();

    size_t indexCount = 0;

    if(attrs.SrcIndices)
    {
        indices.resize(startIndex + attrs.SrcIndices->count);
        VertexIndex* idxDst = indices.data() + startIndex;

        const size_t count = cgltf_accessor_unpack_indices(attrs.SrcIndices,
            idxDst,
            sizeof(uint32_t),
            attrs.SrcIndices->count);

        MLG_CHECK(count == attrs.SrcIndices->count,
            "Failed to unpack all indices for primitive");

        indexCount = attrs.SrcIndices->count;
    }
    else
    {
        // Non-indexed primitive.
        indices.resize(startIndex + attrs.SrcPosition->count);

        VertexIndex* idxDst = indices.data() + startIndex;
        for(size_t i = 0; i < attrs.SrcPosition->count; ++i)
        {
            idxDst[i] = narrow_cast<VertexIndex>(i);
        }

        indexCount = attrs.SrcPosition->count;
    }

    // Change winding from CCW to CW.
    const size_t endIndex = indices.size();
    for(size_t idx = startIndex; idx < endIndex; idx += 3)
    {
        std::swap(indices[idx + 1], indices[idx + 2]);
    }

    return indexCount;
}

static Result<>
GenerateNormals(
    Vertex* vertices, const size_t vertexCount, const VertexIndex* indices, const size_t indexCount)
{
    for(size_t i = 0; i < vertexCount; ++i)
    {
        vertices[i].normal = { 0.0f, 0.0f, 0.0f };
    }

    for(size_t i = 0; i < indexCount; i += 3)
    {
        MLG_LOG_SCOPE("tri {}", i / 3);

        Vertex& v0 = vertices[indices[i + 0]];
        Vertex& v1 = vertices[indices[i + 1]];
        Vertex& v2 = vertices[indices[i + 2]];

        const Vec3f normal0 = (v2.pos - v0.pos).Cross(v1.pos - v0.pos).Normalize();
        const Vec3f normal1 = (v0.pos - v1.pos).Cross(v2.pos - v1.pos).Normalize();
        const Vec3f normal2 = (v1.pos - v2.pos).Cross(v0.pos - v2.pos).Normalize();

        v0.normal += normal0;
        v1.normal += normal1;
        v2.normal += normal2;
    }

    for(size_t i = 0; i < vertexCount; ++i)
    {
        vertices[i].normal = vertices[i].normal.Normalize();
    }

    return Result<>::Ok;
}

static Result<>
CollectModels(const std::vector<CgltfMeshData>& gltfMeshes,
    std::vector<Vertex>& vertices,
    std::vector<VertexIndex>& indices,
    std::vector<MaterialData>& materials,
    std::vector<MeshData>& meshes,
    std::vector<ModelData>& models,
    std::map<const cgltf_mesh*, ModelIndex>& modelIndices)
{
    vertices.clear();
    indices.clear();
    materials.clear();
    meshes.clear();
    models.clear();
    modelIndices.clear();

    size_t meshCount = 0;
    for(const auto & gltfMesh : gltfMeshes)
    {
        meshCount += gltfMesh.Primitives.size();
    }

    meshes.reserve(meshCount);
    models.reserve(gltfMeshes.size());

    uint32_t firstIndex = 0;
    uint32_t baseVertex = 0;

    std::map<const cgltf_material*, size_t> materialMap;

    for(size_t i = 0; i < gltfMeshes.size(); ++i)
    {
        const auto& gltfMesh = gltfMeshes[i];

        MLG_LOG_SCOPE("mesh {}/{}", gltfMesh.Mesh->name ? gltfMesh.Mesh->name : "<unnamed>", i);

        const ModelData model
        {
            .FirstMesh = narrow_cast<uint32_t>(meshes.size()),
            .MeshCount = narrow_cast<uint32_t>(gltfMesh.Primitives.size()),
        };

        for(size_t j = 0; j < gltfMesh.Primitives.size(); ++j)
        {
            MLG_LOG_SCOPE("prim {}", j);

            const auto& primData = gltfMesh.Primitives[j];

            auto vertexCount = CollectVertices(primData.Attributes, vertices);
            MLG_CHECK(vertexCount);

            auto indexCount = CollectIndices(primData.Attributes, indices);
            MLG_CHECK(indexCount);

            if(!primData.Attributes.SrcNormal)
            {
                GenerateNormals(vertices.data() + baseVertex,
                    *vertexCount,
                    indices.data() + firstIndex,
                    *indexCount);
            }

            const cgltf_primitive& prim = *primData.Primitive;

            size_t materialIndex;

            if(materialMap.contains(prim.material))
            {
                materialIndex = materialMap[prim.material];
            }
            else
            {
                auto mtlData = CreateMaterialData(prim.material);
                MLG_CHECK(mtlData);

                materialIndex = materials.size();
                materials.emplace_back(std::move(*mtlData));
                materialMap[prim.material] = materialIndex;
            }

            const MeshData meshData//
            {
                .FirstIndex = narrow_cast<uint32_t>(firstIndex),
                .IndexCount = narrow_cast<uint32_t>(*indexCount),
                .BaseVertex = narrow_cast<uint32_t>(baseVertex),
                .MaterialIndex = narrow_cast<MaterialIndex>(materialIndex),
            };

            meshes.emplace_back(std::move(meshData));

            firstIndex += narrow_cast<uint32_t>(*indexCount);
            baseVertex += narrow_cast<uint32_t>(*vertexCount);
        }

        modelIndices[gltfMesh.Mesh] = narrow_cast<ModelIndex>(models.size());
        models.emplace_back(std::move(model));
    }

    return Result<>::Ok;
}

static void
ConvertRHtoLH(Mat44f& M)
{
    // negate X components of Y and Z basis vectors
    M.m[1].x = -M.m[1].x;
    M.m[2].x = -M.m[2].x;

    // negate Y,Z components of X basis vector
    M.m[0].y = -M.m[0].y;
    M.m[0].z = -M.m[0].z;

    // negate translation X
    M.m[3].x = -M.m[3].x;
}

static Result<size_t>
CollectTransforms(cgltf_node** const childNodes,
    const cgltf_size nodeCount,
    const uint32_t parentIndex,
    std::vector<TransformData>& transforms,
    std::vector<ModelInstance>& modelInstances,
    const std::vector<ModelData>& models,
    const std::map<const cgltf_mesh*, ModelIndex>& modelIndices)
{
    const size_t numTransforms = transforms.size();

    for(cgltf_size i = 0; i < nodeCount; ++i)
    {
        const cgltf_node* srcNode = childNodes[i];

        MLG_LOG_SCOPE("node {}", srcNode->name ? srcNode->name : "<unnamed>");

        const uint32_t transformIndex = narrow_cast<uint32_t>(transforms.size());

        TransformData transformData //
        {
            .ParentIndex = parentIndex
        };

        if(srcNode->has_matrix)
        {
            std::memcpy(&transformData.Transform.m[0].x, srcNode->matrix, sizeof(float) * 16);
            ConvertRHtoLH(transformData.Transform);
        }
        else
        {
            TrsTransformf trs;

            trs.T =
                Vec3f(srcNode->translation[0], srcNode->translation[1], srcNode->translation[2]);

            trs.R = Quatf(srcNode->rotation[0],
                srcNode->rotation[1],
                srcNode->rotation[2],
                srcNode->rotation[3]);

            trs.S = Vec3f(srcNode->scale[0], srcNode->scale[1], srcNode->scale[2]);

            // Convert from right handed to left handed.
            trs.T.x = -trs.T.x;
            trs.R.y = -trs.R.y;
            trs.R.z = -trs.R.z;

            transformData.Transform = trs.ToMatrix();
        }

        transforms.emplace_back(std::move(transformData));

        auto countResult = CollectTransforms(srcNode->children,
            srcNode->children_count,
            transformIndex,
            transforms,
            modelInstances,
            models,
            modelIndices);

        if(!countResult)
        {
            // Something went wrong. Erase the transform we added.
            transforms.resize(transformIndex);
        }
        else if(*countResult == 0 && !srcNode->mesh)
        {
            // No child transforms were added and there's no mesh
            // at this node.  Erase the transform we added.
            transforms.resize(transformIndex);

            // Node has no mesh and no descendents with meshes, skip it
            // This could be a procedurally generated mesh, e.g. the leaves of a tree.
            // In blender the following steps could be used to convert to a mesh:
            // 1. Select the node in the outliner.
            // 2. In the "Layout" editor select the "Object" menu and choose "Apply -> Make
            // Instances Real".
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
        }
        else if(!srcNode->mesh)
        {
            // No mesh so no instance to add.
            continue;
        }
        else if(!modelIndices.contains(srcNode->mesh))
        {
            // This mesh was rejected during the model collection phase.
            // No instance to add.
            continue;
        }
        else
        {
            const ModelIndex modelIndex = modelIndices.at(srcNode->mesh);

            ModelInstance modelInstance //
            {
                .FirstMesh = models[modelIndex].FirstMesh,
                .MeshCount = models[modelIndex].MeshCount,
                .TransformIndex = narrow_cast<TransformIndex>(transformIndex),
            };

            modelInstances.push_back(modelInstance);
        }
    }

    const size_t numAdded = transforms.size() - numTransforms;

    return numAdded;
}

Result<SceneKitSourceData>
CgltfModelLoader::LoadSceneKit(const std::string& path)
{
    std::filesystem::path filePath(path);

    MLG_LOG_SCOPE(filePath.filename().string());

    cgltf_options options = {};
    cgltf_data* gltfData = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &gltfData);
    MLG_CHECK(result == cgltf_result_success, "Failed to load glTF file");

    MLG_CHECK(gltfData->scenes_count > 0, "No scenes found");
    MLG_CHECK(gltfData->scenes_count == 1, "Multiple scenes found, only one scene is supported");

    for(cgltf_size i = 0; i < gltfData->buffer_views_count; ++i)
    {
        const cgltf_buffer_view& buffer_view = gltfData->buffer_views[i];
        MLG_CHECK(!buffer_view.has_meshopt_compression,
            "Unsupported meshopt compression in buffer view {}",
            i);
    }

    cgltf_result loadBuffersResult = cgltf_load_buffers(&options, gltfData, filePath.string().c_str());
    MLG_CHECK(loadBuffersResult == cgltf_result_success, "Failed to load buffers");

    SceneKitSourceData sceneKitData;

    std::vector<CgltfMeshData> gltfMeshes;
    MLG_CHECK(CollectMeshes(gltfData, gltfMeshes));

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;
    std::vector<MaterialData> materials;
    std::vector<MeshData> meshes;
    std::vector<ModelData> models;
    std::map<const cgltf_mesh*, ModelIndex> modelIndices;
    MLG_CHECK(
        CollectModels(gltfMeshes, vertices, indices, materials, meshes, models, modelIndices));

    std::vector<TransformData> transforms;
    std::vector<ModelInstance> modelInstances;
    MLG_CHECK(CollectTransforms(gltfData->scenes[0].nodes,
        gltfData->scenes[0].nodes_count,
        TransformData::kInvalidParentIndex,
        transforms,
        modelInstances,
        models,
        modelIndices));

    cgltf_free(gltfData);

    // Convert local transforms to world space.
    for(auto& transform : transforms)
    {
        if(transform.ParentIndex != TransformData::kInvalidParentIndex)
        {
            const TransformData& parent = transforms[transform.ParentIndex];
            transform.Transform = parent.Transform * transform.Transform;
        }
    }

    sceneKitData.Materials = std::move(materials);
    sceneKitData.Vertices = std::move(vertices);
    sceneKitData.Indices = std::move(indices);
    sceneKitData.Meshes = std::move(meshes);
    sceneKitData.Transforms = std::move(transforms);
    sceneKitData.ModelInstances = std::move(modelInstances);

    return std::move(sceneKitData);
}