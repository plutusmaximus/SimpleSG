#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#define __LOGGER_NAME__ "GLTF"

#include "CgltfModelLoader.h"

#include "DawnSceneKit.h"
#include "FileFetcher.h"
#include "Log.h"
#include "Material.h"
#include "Model.h"
#include "scope_exit.h"
#include "ThreadPool.h"
#include "Vertex.h"
#include "WebgpuHelper.h"

#define CGLTF_IMPLEMENTATION
#include <atomic>
#include <cgltf.h>
#include <filesystem>
#include <map>
#include <stb_image.h>
#include <vector>
#include <webgpu/webgpu_cpp.h>

static constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::RGBA8Unorm;
static constexpr int kNumTextureChannels = 4;

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

struct TextureBuilder
{
    std::string Uri;
    uint32_t Width;
    uint32_t Height;
    uint32_t NumChannels;
    wgpu::Buffer StagingBuffer;
    wgpu::Texture Texture;
    void* MappedMemory{ nullptr };
    const FileFetcher::Request* Request{ nullptr };
    std::atomic<bool> DecodeComplete{ false };
};

struct SceneKitData
{
    std::vector<Vertex> Vertices;
    std::vector<VertexIndex> Indices;
    std::vector<MaterialData> Materials;
    std::vector<ModelData> Models;
    std::vector<uint32_t> MaterialIndices;
    std::vector<ModelInstance> Instances;
    std::vector<TransformData> Transforms;
};

struct TextureCache
{
    std::map<std::string, wgpu::Texture> Textures;

    wgpu::Sampler DefaultSampler;
    wgpu::Texture DefaultTexture;
};

struct SceneData
{
    wgpu::Buffer IndexBuffer;
    wgpu::Buffer VertexBuffer;
    wgpu::Buffer TransformBuffer;
    wgpu::Buffer DrawIndirectBuffer;
    wgpu::Buffer TransformIndexBuffer;

    std::vector<MaterialBinding> MaterialBindings;

    wgpu::BindGroup ColorRenderBindGroup0;
    wgpu::BindGroup TransformBindGroup0;
};

} // namespace

template<typename T, typename U>
static T narrow_cast(U u)
{
    const T t = static_cast<T>(u);
    MLG_ASSERT(static_cast<T>(u) == t, "narrow_cast failed: {} -> {}", u, t);
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
CollectMeshes(const cgltf_data* gltfData, std::vector<CgltfMeshData>& meshes)
{
    meshes.clear();

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

        meshes.emplace_back(std::move(meshData));
    }

    return Result<>::Ok;
}

static Result<MaterialData>
CreateMaterialData(const cgltf_material* material)
{
    std::string baseTextureUri;
    RgbaColorf color{"#FF00FFFF"_rgba};
    float metalness = 0;
    float roughness = 0;

    if(material->has_pbr_metallic_roughness)
    {
        const cgltf_pbr_metallic_roughness& pbr = material->pbr_metallic_roughness;
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

static Result<>
CollectMaterials(const std::vector<CgltfMeshData>& meshes,
    std::vector<MaterialData>& materials,
    std::vector<uint32_t>& materialIndices)
{
    materials.clear();
    materialIndices.clear();
    std::map<const cgltf_material*, size_t> materialMap;

    for(size_t i = 0; i < meshes.size(); ++i)
    {
        const auto& mesh = meshes[i];

        MLG_LOG_SCOPE("mesh {}/{}", mesh.Mesh->name ? mesh.Mesh->name : "<unnamed>", i);

        for(size_t j = 0; j < mesh.Primitives.size(); ++j)
        {
            MLG_LOG_SCOPE("prim {}", j);

            const auto& primData = mesh.Primitives[j];

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
            }

            materialMap[prim.material] = materialIndex;

            materialIndices.push_back(narrow_cast<uint32_t>(materialIndex));
        }
    }

    return Result<>::Ok;
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
CollectModels(const cgltf_data* gltfData,
    SceneKitData& sceneKitData,
    std::map<const cgltf_mesh*, size_t>& modelIndices)
{
    modelIndices.clear();

    std::vector<CgltfMeshData> meshes;
    MLG_CHECK(CollectMeshes(gltfData, meshes));

    std::vector<MaterialData> materials;
    std::vector<uint32_t> materialIndices;
    MLG_CHECK(CollectMaterials(meshes, materials, materialIndices));

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;
    std::vector<ModelData> models;
    std::map<const cgltf_material*, size_t> materialMap;

    uint32_t firstIndex = 0;
    uint32_t baseVertex = 0;

    for(size_t i = 0; i < meshes.size(); ++i)
    {
        const auto& mesh = meshes[i];

        MLG_LOG_SCOPE("mesh {}/{}", mesh.Mesh->name ? mesh.Mesh->name : "<unnamed>", i);

        ModelData model;

        for(size_t j = 0; j < mesh.Primitives.size(); ++j)
        {
            MLG_LOG_SCOPE("prim {}", j);
            const auto& primData = mesh.Primitives[j];

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

            MeshData meshData //
            {
                .FirstIndex = narrow_cast<uint32_t>(firstIndex),
                .BaseVertex = narrow_cast<uint32_t>(baseVertex),
                .IndexCount = narrow_cast<uint32_t>(*indexCount),
            };

            model.Meshes.push_back(meshData);

            firstIndex += narrow_cast<uint32_t>(*indexCount);
            baseVertex += narrow_cast<uint32_t>(*vertexCount);
        }

        modelIndices[mesh.Mesh] = models.size();
        models.push_back(std::move(model));
    }

    sceneKitData.Vertices = std::move(vertices);
    sceneKitData.Indices = std::move(indices);
    sceneKitData.Materials = std::move(materials);
    sceneKitData.Models = std::move(models);
    sceneKitData.MaterialIndices = std::move(materialIndices);

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
    std::vector<ModelInstance>& instances,
    const std::map<const cgltf_mesh*, size_t>& modelIndices)
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
            instances,
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
            ModelInstance instance //
            {
                .ModelIndex = narrow_cast<uint32_t>(modelIndices.at(srcNode->mesh)),
                .TransformIndex = narrow_cast<uint32_t>(transformIndex),
            };

            instances.push_back(instance);
        }
    }

    const size_t numAdded = transforms.size() - numTransforms;

    return numAdded;
}

static std::tuple<wgpu::Buffer, wgpu::Texture>
CreateTexture(wgpu::Device wgpuDevice,
    const uint32_t width,
    const uint32_t height,
    const wgpu::TextureFormat format,
    const std::string& name)
{
    const uint32_t rowStride = width * kNumTextureChannels;
    // Staging buffer rows must be a multiple of 256 bytes.
    const uint32_t alignedRowStride = (rowStride + 255) & ~255;
    const uint32_t stagingSize = alignedRowStride * height;

    wgpu::BufferDescriptor bufDesc = //
        {
            .label = name.c_str(),
            .usage = wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::MapWrite,
            .size = stagingSize,
            .mappedAtCreation = true,
        };

    wgpu::Buffer stagingBuffer = wgpuDevice.CreateBuffer(&bufDesc);

    wgpu::TextureDescriptor desc //
        {
            .label = name.c_str(),
            .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
            .dimension = wgpu::TextureDimension::e2D,
            .size = //
            {
                .width = width,
                .height = height,
                .depthOrArrayLayers = 1,
            },
            .format = format,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };

    wgpu::Texture texture = wgpuDevice.CreateTexture(&desc);

    return {stagingBuffer, texture};
}


static Result<>
StageTexture(wgpu::Device wgpuDevice,
    const std::string& uri,
    const FileFetcher::Request& request,
    std::map<std::string, TextureBuilder>& textureBuilders)
{
    MLG_LOG_SCOPE(uri);

    MLG_DEBUG("Staging texture...");

    int width, height, numChannels;

    if(!stbi_info_from_memory(request.Data.data(),
           narrow_cast<int>(request.Data.size()),
           &width,
           &height,
           &numChannels))
    {
        MLG_ERROR("Error getting image info - {}", uri, stbi_failure_reason());
        return Result<>::Fail;
    }

    MLG_DEBUG("Image info - {} x {} x {}", width, height, numChannels);

    auto [stagingBuffer, texture] = CreateTexture(wgpuDevice,
        narrow_cast<uint32_t>(width),
        narrow_cast<uint32_t>(height),
        kTextureFormat,
        uri);

    void* mappedMemory = stagingBuffer.GetMappedRange();
    MLG_CHECK(mappedMemory, "Failed to map staging buffer for texture upload");

    auto [it, inserted] = textureBuilders.try_emplace(uri,
        uri,
        narrow_cast<uint32_t>(width),
        narrow_cast<uint32_t>(height),
        narrow_cast<uint32_t>(numChannels),
        stagingBuffer,
        texture,
        mappedMemory,
        &request);

    TextureBuilder& builder = it->second;

    auto decode = [&builder]()
    {
        MLG_LOG_SCOPE(builder.Uri);

        MLG_DEBUG("Decoding...");

        int width, height, numChannels;
        stbi_uc* data = stbi_load_from_memory(builder.Request->Data.data(),
            narrow_cast<int>(builder.Request->Data.size()),
            &width,
            &height,
            &numChannels,
            kNumTextureChannels);

        if(!data)
        {
            MLG_ERROR("Error decoding - {}", stbi_failure_reason());
        }
        else
        {
            uint8_t* dst = (uint8_t*)builder.MappedMemory;
            const uint8_t* src = data;
            const uint32_t rowStride = width * kNumTextureChannels;
            const uint32_t alignedRowStride = (rowStride + 255) & ~255;
            for(uint32_t y = 0; y < builder.Height;
                ++y, dst += alignedRowStride, src += rowStride)
            {
                ::memcpy(dst, src, rowStride);
            }
            stbi_image_free(data);
        }

        builder.DecodeComplete = true;
    };

    ThreadPool::Enqueue(decode);

    return Result<>::Ok;
}

static Result<>
CommitTexture(TextureBuilder& builder, wgpu::CommandEncoder encoder)
{
    MLG_LOG_SCOPE(builder.Uri);

    MLG_DEBUG("Committing texture...");

    const uint32_t rowStride = builder.Width * kNumTextureChannels;
    const uint32_t alignedRowStride = (rowStride + 255) & ~255;

    wgpu::TexelCopyBufferInfo copySrc = //
        {
            .layout = //
            {
                .offset = 0,
                .bytesPerRow = alignedRowStride,
                .rowsPerImage = builder.Height,
            },
            .buffer = builder.StagingBuffer,
        };

    wgpu::TexelCopyTextureInfo copyDst = //
        {
            .texture = builder.Texture,
            .mipLevel = 0,
            .origin = { 0, 0, 0 },
        };

    wgpu::Extent3D copySize = //
        {
            .width = builder.Width,
            .height = builder.Height,
            .depthOrArrayLayers = 1,
        };

    encoder.CopyBufferToTexture(&copySrc, &copyDst, &copySize);

    return Result<>::Ok;
}

static Result<>
FetchTextures(wgpu::Device wgpuDevice,
    std::filesystem::path basePath,
    std::span<const MaterialData> materials,
    TextureCache& textureCache)
{
    std::map<std::string, FileFetcher::Request> fetchRequests;

    for(const auto& mtl : materials)
    {
        if(mtl.BaseTextureUri.empty())
        {
            // No base texture for this material, skip it.
            continue;
        }

        if(fetchRequests.contains(mtl.BaseTextureUri))
        {
            // We've already issued a fetch for this texture, skip it.
            continue;
        }

        if(textureCache.Textures.contains(mtl.BaseTextureUri))
        {
            // We've already loaded this texture, skip it.
            continue;
        }

        MLG_LOG_SCOPE(mtl.BaseTextureUri);

        auto [btIt, btInserted] =
            fetchRequests.try_emplace(mtl.BaseTextureUri, (basePath / mtl.BaseTextureUri).string());

        if(btInserted)
        {
            MLG_DEBUG("Fetching texture...");

            if(!FileFetcher::Fetch(btIt->second))
            {
                MLG_WARN("Failed to fetch texture: {}", btIt->second.FilePath);
            }
        }
    }

    std::map<std::string, TextureBuilder> textureBuilders;

    bool pending;

    do
    {
        pending = false;

        FileFetcher::ProcessCompletions();

        for(auto& [uri, request] : fetchRequests)
        {
            if(request.IsPending())
            {
                pending = true;
            }
            else if(!textureBuilders.contains(uri))
            {
                auto result = StageTexture(wgpuDevice, uri, request, textureBuilders);
                if(!result)
                {
                    MLG_WARN("Failed to stage texture: {}", uri);
                }
            }
        }
    } while(pending);

    MLG_DEBUG("Loaded {} textures", fetchRequests.size());

    wgpu::CommandEncoderDescriptor encDesc = {};
    wgpu::CommandEncoder encoder = wgpuDevice.CreateCommandEncoder(&encDesc);

    do
    {
        pending = false;

        for(auto& [uri, builder] : textureBuilders)
        {
            MLG_LOG_SCOPE(uri);

            if(!builder.DecodeComplete)
            {
                pending = true;
            }
            else if(builder.StagingBuffer.GetMapState() == wgpu::BufferMapState::Mapped)
            {
                builder.StagingBuffer.Unmap();
                CommitTexture(builder, encoder);
            }
        }
    } while(pending);

    wgpu::CommandBuffer commandBuffer = encoder.Finish();

    // TODO - change API to separate creating a resource from populating it
    wgpuDevice.GetQueue().Submit(1, &commandBuffer);

    for(auto& [uri, builder] : textureBuilders)
    {
        textureCache.Textures[uri] = builder.Texture;
    }

    return Result<>::Ok;
}

static Result<>
CreateMaterialDefaults(wgpu::Device wgpuDevice, TextureCache& textureCache)
{
    constexpr uint32_t kDefaultTextureWidth = 128;
    constexpr uint32_t kDefaultTextureHeight = 128;

    auto [stagingBuffer, defaultTexture] = CreateTexture(wgpuDevice,
        kDefaultTextureWidth,
        kDefaultTextureHeight,
        kTextureFormat,
        "DefaultTexture");

    uint8_t* data = static_cast<uint8_t*>(stagingBuffer.GetMappedRange());

    for(uint32_t y = 0; y < kDefaultTextureHeight; ++y)
    {
        for(uint32_t x = 0; x < kDefaultTextureWidth; ++x, data += 4)
        {
            //Magenta
            data[0] = 0xFF;
            data[1] = 0x00;
            data[2] = 0xFF;
            data[3] = 0xFF;
        }
    }
    stagingBuffer.Unmap();

    wgpu::SamplerDescriptor samplerDesc//
    {
        .addressModeU = wgpu::AddressMode::Repeat,
        .addressModeV = wgpu::AddressMode::Repeat,
        .addressModeW = wgpu::AddressMode::Repeat,
        .magFilter = wgpu::FilterMode::Linear,
        .minFilter = wgpu::FilterMode::Linear,
        .mipmapFilter = wgpu::MipmapFilterMode::Linear,
    };

    wgpu::Sampler defaultSampler = wgpuDevice.CreateSampler(&samplerDesc);

    textureCache.DefaultTexture = defaultTexture;
    textureCache.DefaultSampler = defaultSampler;

    return Result<>::Ok;
}

static Result<>
CreateBindGroups(wgpu::Device wgpuDevice, SceneData& sceneData)
{
    auto colorPipelineLayouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(colorPipelineLayouts);
    auto transformPipelineLayouts = WebgpuHelper::GetTransformPipelineLayouts();
    MLG_CHECK(transformPipelineLayouts);

    // Color pipeline bind group 0
    {
        wgpu::BindGroupEntry bgEntries[] =//
        {
            {
                .binding = 0,
                .buffer = sceneData.TransformBuffer,
                .offset = 0,
                .size = sceneData.TransformBuffer.GetSize(),
            },
            {
                .binding = 1,
                .buffer = sceneData.TransformIndexBuffer,
                .offset = 0,
                .size = sceneData.TransformIndexBuffer.GetSize(),
            },
        };

        wgpu::BindGroupDescriptor bgDesc = //
            {
                .label = "ColorPipelineBindGroup0",
                .layout = colorPipelineLayouts->Bindgroup0Layout,
                .entryCount = std::size(bgEntries),
                .entries = bgEntries,
            };

        sceneData.ColorRenderBindGroup0 = wgpuDevice.CreateBindGroup(&bgDesc);
        MLG_CHECK(sceneData.ColorRenderBindGroup0,
            "Failed to create bind group 0 for color pipeline");
    }

    // Transform pipeline bind group 0
    {
        wgpu::BindGroupEntry bgEntries[] =//
        {
            {
                .binding = 0,
                .buffer = sceneData.TransformBuffer,
                .offset = 0,
                .size = sceneData.TransformBuffer.GetSize(),
            },
        };

        wgpu::BindGroupDescriptor bgDesc = //
            {
                .label = "TransformPipelineBindGroup0",
                .layout = transformPipelineLayouts->Bindgroup0Layout,
                .entryCount = std::size(bgEntries),
                .entries = bgEntries,
            };

        sceneData.TransformBindGroup0 = wgpuDevice.CreateBindGroup(&bgDesc);
        MLG_CHECK(sceneData.TransformBindGroup0,
            "Failed to create bind group 0 for transform pipeline");
    }

    return Result<>::Ok;
}

static wgpu::Buffer
CreateGpuBuffer(wgpu::Device device, wgpu::BufferUsage usage, const size_t size, const char* name)
{
    wgpu::BufferDescriptor bufferDesc //
        {
            .label = name,
            .usage = usage,
            .size = size,
            .mappedAtCreation = true,
        };

    return device.CreateBuffer(&bufferDesc);
}

static Result<>
BuildMaterials(wgpu::Device wgpuDevice,
    std::span<MaterialData> materials,
    const TextureCache& textureCache,
    SceneData& sceneData)
{
    std::vector<MaterialBinding> materialBindings;

    const size_t alignedSizeofMtlConstants = WebgpuHelper::AlignUniformBuffer<MaterialConstants>();
    const size_t sizeofMtlConstantsBuffer = materials.size() * alignedSizeofMtlConstants;

    wgpu::Buffer materialConstantsBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
        sizeofMtlConstantsBuffer,
        "MaterialConstantsBuffer");

    void* mappedRange = materialConstantsBuffer.GetMappedRange();
    MLG_CHECK(mappedRange, "Failed to map MaterialConstantsBuffer");

    uint8_t* dst = reinterpret_cast<uint8_t*>(mappedRange);

    for(const auto& mtl : materials)
    {
        ::new (dst) MaterialConstants //
            {
                mtl.Color,
                mtl.Metalness,
                mtl.Roughness
            };

        dst += alignedSizeofMtlConstants;
    }

    materialConstantsBuffer.Unmap();

    auto layouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(layouts);

    size_t mtlConstantsOffset = 0;

    for(const auto& mtl : materials)
    {
        wgpu::Texture baseTexture = mtl.BaseTextureUri.empty()
                                        ? textureCache.DefaultTexture
                                        : textureCache.Textures.at(mtl.BaseTextureUri);

        wgpu::BindGroupEntry bgEntries[]//
        {
            {
                .binding = 0,
                .textureView = baseTexture.CreateView(),
            },
            {
                .binding = 1,
                .sampler = textureCache.DefaultSampler,
            },
            {
                .binding = 2,
                .buffer = materialConstantsBuffer,
                .offset = mtlConstantsOffset,
                .size = alignedSizeofMtlConstants,
            },
        };

        wgpu::BindGroupDescriptor bindGroupDesc //
        {
            .label = "MaterialBindGroup",
            .layout = layouts->Bindgroup2Layout,
            .entryCount = std::size(bgEntries),
            .entries = bgEntries,
        };

        wgpu::BindGroup materialBindGroup = wgpuDevice.CreateBindGroup(&bindGroupDesc);

        materialBindings.emplace_back(std::move(materialBindGroup));

        mtlConstantsOffset += alignedSizeofMtlConstants;
    }

    sceneData.MaterialBindings = std::move(materialBindings);

    return Result<>::Ok;
}

static Result<>
BuildVertexBuffer(wgpu::Device wgpuDevice, std::span<Vertex> vertices, SceneData& sceneData)
{
    const size_t sizeofBuffer = vertices.size() * sizeof(Vertex);
    wgpu::Buffer vertexBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst,
        sizeofBuffer,
        "VertexBuffer");

    void* vbMapped = vertexBuffer.GetMappedRange();
    MLG_CHECK(vbMapped, "Failed to map VertexBuffer");

    ::memcpy(vbMapped, vertices.data(), sizeofBuffer);

    vertexBuffer.Unmap();

    sceneData.VertexBuffer = vertexBuffer;

    return Result<>::Ok;
}

static Result<>
BuildIndexBuffer(wgpu::Device wgpuDevice, std::span<VertexIndex> indices, SceneData& sceneData)
{
    const size_t sizeofBuffer = indices.size() * sizeof(VertexIndex);
    wgpu::Buffer indexBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst,
        sizeofBuffer,
        "IndexBuffer");

    void* ibMapped = indexBuffer.GetMappedRange();
    MLG_CHECK(ibMapped, "Failed to map IndexBuffer");

    ::memcpy(ibMapped, indices.data(), sizeofBuffer);

    indexBuffer.Unmap();

    sceneData.IndexBuffer = indexBuffer;

    return Result<>::Ok;
}

static Result<>
BuildTransformBuffer(wgpu::Device wgpuDevice, std::span<TransformData> transforms, SceneData& sceneData)
{
    const size_t sizeofBuffer = transforms.size() * sizeof(Mat44f);

    wgpu::Buffer transformBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
        sizeofBuffer,
        "TransformBuffer");

    void* mappedRange = transformBuffer.GetMappedRange();
    MLG_CHECK(mappedRange, "Failed to map TransformBuffer");

    Mat44f* dst = reinterpret_cast<Mat44f*>(mappedRange);

    for(const TransformData& transform : transforms)
    {
        *dst++ = transform.Transform;
    }

    transformBuffer.Unmap();

    sceneData.TransformBuffer = transformBuffer;

    return Result<>::Ok;
}

static Result<>
BuildDrawBuffers(wgpu::Device wgpuDevice, const SceneKitData& sceneKitData, SceneData& sceneData)
{
    size_t meshInstanceCount = 0;
    for(const auto& instance : sceneKitData.Instances)
    {
        const auto& model = sceneKitData.Models[instance.ModelIndex];
        meshInstanceCount += model.Meshes.size();
    }

    const size_t sizeofDrawIndirectBuffer = meshInstanceCount * sizeof(DrawIndirectBufferParams);
    const size_t sizeofTransformIndexBuffer = meshInstanceCount * sizeof(uint32_t);

    sceneData.DrawIndirectBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst,
        sizeofDrawIndirectBuffer,
        "DrawIndirectBuffer");

    sceneData.TransformIndexBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
        sizeofTransformIndexBuffer,
        "TransformIndexBuffer");

    void* diMapped = sceneData.DrawIndirectBuffer.GetMappedRange();
    MLG_CHECK(diMapped, "Failed to map DrawIndirectBuffer");

    auto cleanupDrawParams = scope_exit([&]() { sceneData.DrawIndirectBuffer.Unmap(); });

    void* transformIndexMapped = sceneData.TransformIndexBuffer.GetMappedRange();
    MLG_CHECK(transformIndexMapped, "Failed to map TransformIndexBuffer");

    auto cleanupTransformIndex = scope_exit([&]() { sceneData.TransformIndexBuffer.Unmap(); });

    DrawIndirectBufferParams* drawParams = reinterpret_cast<DrawIndirectBufferParams*>(diMapped);
    uint32_t* transformIndex = reinterpret_cast<uint32_t*>(transformIndexMapped);

    uint32_t firstInstance = 0;

    for(const auto& instance : sceneKitData.Instances)
    {
        for(const auto& mesh : sceneKitData.Models[instance.ModelIndex].Meshes)
        {
            *drawParams++ = //
            {
                .IndexCount = mesh.IndexCount,
                .InstanceCount = 1,
                .FirstIndex = mesh.FirstIndex,
                .BaseVertex = mesh.BaseVertex,
                .FirstInstance = firstInstance,
            };

            *transformIndex++ = instance.TransformIndex;
            ++firstInstance;
        }
    }

    return Result<>::Ok;
}

Result<SceneKit*>
CgltfModelLoader::LoadSceneKit(wgpu::Device& wgpuDevice, const std::string& path)
{
    std::filesystem::path filePath(path);

    MLG_LOG_SCOPE(filePath.filename().string());

    cgltf_options options = {};
    cgltf_data* data = nullptr;
    cgltf_result result = cgltf_parse_file(&options, path.c_str(), &data);
    MLG_CHECK(result == cgltf_result_success, "Failed to load glTF file");

    MLG_CHECK(data->scenes_count > 0, "No scenes found");
    MLG_CHECK(data->scenes_count == 1, "Multiple scenes found, only one scene is supported");

    for(cgltf_size i = 0; i < data->buffer_views_count; ++i)
    {
        const cgltf_buffer_view& buffer_view = data->buffer_views[i];
        MLG_CHECK(!buffer_view.has_meshopt_compression,
            "Unsupported meshopt compression in buffer view {}",
            i);
    }

    cgltf_result loadBuffersResult = cgltf_load_buffers(&options, data, filePath.string().c_str());
    MLG_CHECK(loadBuffersResult == cgltf_result_success, "Failed to load buffers");

    SceneKitData sceneKitData;
    SceneData sceneData;
    TextureCache textureCache;
    std::map<const cgltf_mesh*, size_t> modelIndices;

    MLG_CHECK(CollectModels(data, sceneKitData, modelIndices));

    MLG_CHECK(CollectTransforms(data->scenes[0].nodes,
        data->scenes[0].nodes_count,
        TransformData::kInvalidParentIndex,
        sceneKitData.Transforms,
        sceneKitData.Instances,
        modelIndices));

    // Convert local transforms to world space.
    for(auto& transform : sceneKitData.Transforms)
    {
        if(transform.ParentIndex != TransformData::kInvalidParentIndex)
        {
            const Mat44f& parent = sceneKitData.Transforms[transform.ParentIndex].Transform;
            transform.Transform = parent * transform.Transform;
        }
    }

    MLG_CHECK(FetchTextures(wgpuDevice, filePath.parent_path(), sceneKitData.Materials, textureCache));

    MLG_CHECK(BuildVertexBuffer(wgpuDevice, sceneKitData.Vertices, sceneData));
    MLG_CHECK(BuildIndexBuffer(wgpuDevice, sceneKitData.Indices, sceneData));

    MLG_CHECK(BuildTransformBuffer(wgpuDevice, sceneKitData.Transforms, sceneData));
    MLG_CHECK(BuildDrawBuffers(wgpuDevice, sceneKitData, sceneData));

    MLG_CHECK(CreateMaterialDefaults(wgpuDevice, textureCache));

    MLG_CHECK(BuildMaterials(wgpuDevice, sceneKitData.Materials, textureCache, sceneData));

    MLG_CHECK(CreateBindGroups(wgpuDevice, sceneData));

    cgltf_free(data);

    DawnSceneKit* sceneKit = new DawnSceneKit(
        sceneData.IndexBuffer,
        sceneData.VertexBuffer,
        sceneData.TransformBuffer,
        sceneData.DrawIndirectBuffer,
        sceneData.TransformIndexBuffer,
        sceneData.ColorRenderBindGroup0,
        sceneData.TransformBindGroup0,
        std::move(sceneData.MaterialBindings),
        std::move(sceneKitData.MaterialIndices));

    return sceneKit;
}