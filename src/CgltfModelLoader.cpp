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
    const FileFetcher::Request* Request{ nullptr };
    wgpu::Buffer StagingBuffer;
    wgpu::Texture Texture;
    void* MappedMemory{ nullptr };
    std::atomic<bool> DecodeComplete{ false };
};

struct ModelData
{
    uint32_t FirstMesh;
    uint32_t MeshCount;
};

struct SceneKitData
{
    std::vector<Vertex> Vertices;
    std::vector<VertexIndex> Indices;
    std::vector<MaterialData> Materials;
    std::vector<TransformData> Transforms;
    std::vector<MeshData> Meshes;
    std::vector<ModelInstance> ModelInstances;
};

struct TextureCache
{
    std::unordered_map<std::string, wgpu::Texture> Textures;

    wgpu::Sampler DefaultSampler;
    wgpu::Texture DefaultTexture;
};

struct ColorPipelineResources
{
    wgpu::Buffer TransformBuffer;
    wgpu::Buffer MaterialConstantsBuffer;
    wgpu::Buffer MeshDrawDataBuffer;
};

struct TransformPipelineResources
{
    wgpu::Buffer TransformBuffer;
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

static Result<wgpu::Texture>
CreateTexture(wgpu::Device wgpuDevice,
    const uint32_t width,
    const uint32_t height,
    const wgpu::TextureFormat format,
    const std::string& name)
{
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

    return texture;
}

static Result<wgpu::Buffer>
CreateTextureStagingBuffer(wgpu::Device wgpuDevice, wgpu::Texture texture, const std::string& name)
{
    const uint32_t rowStride = texture.GetWidth() * kNumTextureChannels;
    // Staging buffer rows must be a multiple of 256 bytes.
    const uint32_t alignedRowStride = (rowStride + 255) & ~255;
    const uint32_t stagingSize = alignedRowStride * texture.GetHeight();

    wgpu::BufferDescriptor bufDesc = //
        {
            .label = name.c_str(),
            .usage = wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::MapWrite,
            .size = stagingSize,
            .mappedAtCreation = true,
        };

    wgpu::Buffer stagingBuffer = wgpuDevice.CreateBuffer(&bufDesc);

    return stagingBuffer;
}

static Result<>
StageTexture(wgpu::Device wgpuDevice, TextureBuilder& builder)
{
    MLG_DEBUG("Staging texture...");

    int width, height, numChannels;

    if(!stbi_info_from_memory(builder.Request->Data.data(),
           narrow_cast<int>(builder.Request->Data.size()),
           &width,
           &height,
           &numChannels))
    {
        MLG_ERROR("Error getting image info - {}", builder.Uri, stbi_failure_reason());
        return Result<>::Fail;
    }

    MLG_DEBUG("Image info - {} x {} x {}", width, height, numChannels);

    auto texture = CreateTexture(wgpuDevice,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        kTextureFormat,
        builder.Uri);

    MLG_CHECK(texture);

    auto stagingBuffer = CreateTextureStagingBuffer(wgpuDevice, *texture, builder.Uri);
    MLG_CHECK(stagingBuffer);

    void* mappedMemory = stagingBuffer->GetMappedRange();
    MLG_CHECK(mappedMemory, "Failed to map staging buffer for texture upload");

    builder.Texture = *texture;
    builder.StagingBuffer = *stagingBuffer;
    builder.MappedMemory = mappedMemory;

    auto decode = [](void* userData)
    {
        auto builder = reinterpret_cast<TextureBuilder*>(userData);
        MLG_LOG_SCOPE(builder->Uri);

        MLG_DEBUG("Decoding...");

        int width, height, numChannels;
        stbi_uc* data = stbi_load_from_memory(builder->Request->Data.data(),
            narrow_cast<int>(builder->Request->Data.size()),
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
            uint8_t* dst = (uint8_t*)builder->MappedMemory;
            const uint8_t* src = data;
            const uint32_t rowStride = width * kNumTextureChannels;
            const uint32_t alignedRowStride = (rowStride + 255) & ~255;
            for(uint32_t y = 0; y < static_cast<uint32_t>(height);
                ++y, dst += alignedRowStride, src += rowStride)
            {
                ::memcpy(dst, src, rowStride);
            }
            stbi_image_free(data);
        }

        builder->DecodeComplete = true;
    };

    ThreadPool::Enqueue(decode, &builder);

    return Result<>::Ok;
}

static Result<>
CommitTexture(wgpu::Texture texture, wgpu::Buffer stagingBuffer, wgpu::CommandEncoder encoder)
{
    MLG_DEBUG("Committing texture...");

    const uint32_t rowStride = texture.GetWidth() * kNumTextureChannels;
    const uint32_t alignedRowStride = (rowStride + 255) & ~255;

    wgpu::TexelCopyBufferInfo copySrc = //
        {
            .layout = //
            {
                .offset = 0,
                .bytesPerRow = alignedRowStride,
                .rowsPerImage = texture.GetHeight(),
            },
            .buffer = stagingBuffer,
        };

    wgpu::TexelCopyTextureInfo copyDst = //
        {
            .texture = texture,
            .mipLevel = 0,
            .origin = { 0, 0, 0 },
        };

    wgpu::Extent3D copySize = //
        {
            .width = texture.GetWidth(),
            .height = texture.GetHeight(),
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
    std::unordered_map<std::string, FileFetcher::Request> fetchRequests;

    for(const auto& mtl : materials)
    {
        if(mtl.BaseTextureUri.empty())
        {
            // No base texture for this material, skip it.
            continue;
        }

        if(textureCache.Textures.contains(mtl.BaseTextureUri))
        {
            // We've already loaded this texture, skip it.
            continue;
        }

        MLG_LOG_SCOPE(mtl.BaseTextureUri);

        // Set the default texture for this URI in the cache so that if the fetch or subsequent
        // staging fails we will have a valid texture to use.
        textureCache.Textures[mtl.BaseTextureUri] = textureCache.DefaultTexture;

        auto [it, inserted] =
            fetchRequests.try_emplace(mtl.BaseTextureUri, (basePath / mtl.BaseTextureUri).string());

        if(!inserted)
        {
            // We've already issued a fetch for this texture, skip it.
             continue;
        }

        MLG_DEBUG("Fetching texture...");

        FileFetcher::Request& request = it->second;

        if(!FileFetcher::Fetch(request))
        {
            MLG_WARN("Failed to fetch texture: {}", request.FilePath);
        }
    }

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
        }
    } while(pending);

    MLG_DEBUG("Loaded {} textures", fetchRequests.size());

    size_t builderCount = 0;
    for(auto& [uri, request] : fetchRequests)
    {
        if(request.Succeeded())
        {
            ++builderCount;
        }
    }

    std::vector<TextureBuilder> textureBuilders(builderCount);
    size_t builderIndex = 0;

    for(auto& [uri, request] : fetchRequests)
    {
        if(!request.Succeeded())
        {
            continue;
        }

        MLG_LOG_SCOPE(uri);

        TextureBuilder& builder = textureBuilders[builderIndex++];

        builder.Uri = uri;
        builder.Request = &request;

        auto result = StageTexture(wgpuDevice, builder);
        if(!result)
        {
            MLG_WARN("Failed to stage texture: {}", builder.Uri);
        }
    }

    do
    {
        pending = false;

        for(auto& builder : textureBuilders)
        {
            MLG_LOG_SCOPE(builder.Uri);

            if(!builder.DecodeComplete)
            {
                pending = true;
            }
        }
    } while(pending);

    wgpu::CommandEncoderDescriptor encDesc = {};
    wgpu::CommandEncoder encoder = wgpuDevice.CreateCommandEncoder(&encDesc);

    for(auto& builder : textureBuilders)
    {
        MLG_LOG_SCOPE(builder.Uri);

        builder.StagingBuffer.Unmap();
        CommitTexture(builder.Texture, builder.StagingBuffer, encoder);
    }

    wgpu::CommandBuffer commandBuffer = encoder.Finish();

    wgpuDevice.GetQueue().Submit(1, &commandBuffer);

    for(auto& builder : textureBuilders)
    {
        textureCache.Textures[builder.Uri] = builder.Texture;
    }

    return Result<>::Ok;
}

static Result<TextureCache>
CreateTextureCache(wgpu::Device wgpuDevice)
{
    constexpr uint32_t kDefaultTextureWidth = 128;
    constexpr uint32_t kDefaultTextureHeight = 128;

    auto defaultTexture = CreateTexture(wgpuDevice,
        kDefaultTextureWidth,
        kDefaultTextureHeight,
        kTextureFormat,
        "DefaultTexture");

    MLG_CHECK(defaultTexture);

    auto stagingBuffer =
        CreateTextureStagingBuffer(wgpuDevice, *defaultTexture, "DefaultTexture");

    MLG_CHECK(stagingBuffer);

    uint8_t* data = static_cast<uint8_t*>(stagingBuffer->GetMappedRange());

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

    stagingBuffer->Unmap();

    wgpu::CommandEncoderDescriptor encDesc = {};
    wgpu::CommandEncoder encoder = wgpuDevice.CreateCommandEncoder(&encDesc);

    MLG_CHECK(CommitTexture(*defaultTexture, *stagingBuffer, encoder));

    wgpu::CommandBuffer commandBuffer = encoder.Finish();

    // TODO - change API to separate creating a resource from populating it
    wgpuDevice.GetQueue().Submit(1, &commandBuffer);

    wgpu::SamplerDescriptor samplerDesc//
    {
        .addressModeU = wgpu::AddressMode::Repeat,
        .addressModeV = wgpu::AddressMode::Repeat,
        .addressModeW = wgpu::AddressMode::Repeat,
        .magFilter = wgpu::FilterMode::Linear,
        .minFilter = wgpu::FilterMode::Linear,
        .mipmapFilter = wgpu::MipmapFilterMode::Linear,
    };

    TextureCache textureCache;

    textureCache.DefaultTexture = *defaultTexture;
    textureCache.DefaultSampler = wgpuDevice.CreateSampler(&samplerDesc);

    return std::move(textureCache);
}

static Result<wgpu::BindGroup>
CreateColorPipelineBindGroup0(wgpu::Device wgpuDevice,
    ColorPipelineResources& colorPipelineResources)
{
    auto colorPipelineLayouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(colorPipelineLayouts);

    wgpu::BindGroupEntry bgEntries[] =//
    {
        {
            .binding = 0,
            .buffer = colorPipelineResources.MeshDrawDataBuffer,
            .offset = 0,
            .size = colorPipelineResources.MeshDrawDataBuffer.GetSize(),
        },
        {
            .binding = 1,
            .buffer = colorPipelineResources.TransformBuffer,
            .offset = 0,
            .size = colorPipelineResources.TransformBuffer.GetSize(),
        },
        {
            .binding = 2,
            .buffer = colorPipelineResources.MaterialConstantsBuffer,
            .offset = 0,
            .size = colorPipelineResources.MaterialConstantsBuffer.GetSize(),
        },
    };

    wgpu::BindGroupDescriptor bgDesc = //
        {
            .label = "ColorPipelineBindGroup0",
            .layout = colorPipelineLayouts->Bindgroup0Layout,
            .entryCount = std::size(bgEntries),
            .entries = bgEntries,
        };

    wgpu::BindGroup bindGroup = wgpuDevice.CreateBindGroup(&bgDesc);
    MLG_CHECK(bindGroup,
        "Failed to create bind group 0 for color pipeline");

    return bindGroup;
}

static Result<wgpu::BindGroup>
CreateTransformPipelineBindGroup0(wgpu::Device wgpuDevice,
    TransformPipelineResources& transformPipelineResources)
{
    auto transformPipelineLayouts = WebgpuHelper::GetTransformPipelineLayouts();
    MLG_CHECK(transformPipelineLayouts);

    wgpu::BindGroupEntry bgEntries[] =//
    {
        {
            .binding = 0,
            .buffer = transformPipelineResources.TransformBuffer,
            .offset = 0,
            .size = transformPipelineResources.TransformBuffer.GetSize(),
        },
    };

    wgpu::BindGroupDescriptor bgDesc = //
        {
            .label = "TransformPipelineBindGroup0",
            .layout = transformPipelineLayouts->Bindgroup0Layout,
            .entryCount = std::size(bgEntries),
            .entries = bgEntries,
        };

    wgpu::BindGroup bindGroup = wgpuDevice.CreateBindGroup(&bgDesc);
    MLG_CHECK(bindGroup,
        "Failed to create bind group 0 for transform pipeline");

    return bindGroup;
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

static Result<wgpu::BindGroup>
CreateMaterialBindGroup(wgpu::Device wgpuDevice,
    const MaterialData& material,
    const TextureCache& textureCache)
{
    auto layouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(layouts);

    wgpu::Texture baseTexture = material.BaseTextureUri.empty()
                                    ? textureCache.DefaultTexture
                                    : textureCache.Textures.at(material.BaseTextureUri);

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
    };

    wgpu::BindGroupDescriptor bindGroupDesc //
    {
        .label = "MaterialBindGroup",
        .layout = layouts->Bindgroup2Layout,
        .entryCount = std::size(bgEntries),
        .entries = bgEntries,
    };

    wgpu::BindGroup bindGroup = wgpuDevice.CreateBindGroup(&bindGroupDesc);

    return bindGroup;
}

static Result<>
CreateMaterialBindGroups(wgpu::Device wgpuDevice,
    std::span<MaterialData> materials,
    const TextureCache& textureCache,
    std::vector<wgpu::BindGroup>& materialBindGroups)
{
    materialBindGroups.clear();

    materialBindGroups.reserve(materials.size());

    auto layouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(layouts);

    for(const auto& mtl : materials)
    {
        auto bindGroup = CreateMaterialBindGroup(wgpuDevice, mtl, textureCache);
        MLG_CHECK(bindGroup);

        materialBindGroups.emplace_back(std::move(*bindGroup));
    }

    return Result<>::Ok;
}

static Result<wgpu::Buffer>
BuildVertexBuffer(wgpu::Device wgpuDevice, std::span<Vertex> vertices)
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

    return vertexBuffer;
}

static Result<wgpu::Buffer>
BuildIndexBuffer(wgpu::Device wgpuDevice, std::span<VertexIndex> indices)
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

    return indexBuffer;
}

static Result<wgpu::Buffer>
BuildTransformBuffer(wgpu::Device wgpuDevice, std::span<TransformData> transforms)
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

    return transformBuffer;
}

static Result<wgpu::Buffer>
BuildMaterialConstantsBuffer(
    wgpu::Device wgpuDevice, std::span<MaterialData> materials)
{
    const size_t sizeofMaterialConstantsBuffer = materials.size() * sizeof(MaterialConstants);

    wgpu::Buffer materialConstantsBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
        sizeofMaterialConstantsBuffer,
        "MaterialConstantsBuffer");

    void* mtlConstantsMapped = materialConstantsBuffer.GetMappedRange();
    MLG_CHECK(mtlConstantsMapped, "Failed to map MaterialConstantsBuffer");

    MaterialConstants* dst = reinterpret_cast<MaterialConstants*>(mtlConstantsMapped);

    for(const auto& mtl : materials)
    {
        ::new(dst++) MaterialConstants //
        {
            .Color = mtl.Color,
            .Metalness = mtl.Metalness,
            .Roughness = mtl.Roughness,
        };
    }

    materialConstantsBuffer.Unmap();

    return materialConstantsBuffer;
}

static Result<wgpu::Buffer>
BuildDrawIndirectBuffer(wgpu::Device wgpuDevice,
    std::span<const MeshData> meshDatas,
    std::span<const ModelInstance> modelInstances)
{
    size_t meshInstanceCount = 0;
    for(const auto& modelInstance : modelInstances)
    {
        meshInstanceCount += modelInstance.MeshCount;
    }

    const size_t sizeofDrawIndirectBuffer = meshInstanceCount * sizeof(DrawIndirectBufferParams);

    auto drawIndirectBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst,
        sizeofDrawIndirectBuffer,
        "DrawIndirectBuffer");

    void* diMapped = drawIndirectBuffer.GetMappedRange();
    MLG_CHECK(diMapped, "Failed to map DrawIndirectBuffer");

    DrawIndirectBufferParams* drawParams = reinterpret_cast<DrawIndirectBufferParams*>(diMapped);

    uint32_t meshCount = 0;

    for(const auto& modelInstance : modelInstances)
    {
        std::span<const MeshData> meshes //
            {
                meshDatas.data() + modelInstance.FirstMesh,
                modelInstance.MeshCount,
            };

        for(const auto& meshData : meshes)
        {
            drawParams[meshCount] = //
            {
                .IndexCount = meshData.IndexCount,
                .InstanceCount = 1,
                .FirstIndex = meshData.FirstIndex,
                .BaseVertex = meshData.BaseVertex,
                .FirstInstance = meshCount,
            };

            ++meshCount;
        }
    }

    drawIndirectBuffer.Unmap();

    return drawIndirectBuffer;
}

static Result<wgpu::Buffer>
BuildMeshDrawDataBuffer(wgpu::Device wgpuDevice,
    std::span<const MeshData> meshDatas,
    std::span<const ModelInstance> modelInstances)
{
    size_t meshInstanceCount = 0;
    for(const auto& modelInstance : modelInstances)
    {
        meshInstanceCount += modelInstance.MeshCount;
    }

    const size_t sizeofMeshDrawDataBuffer = meshInstanceCount * sizeof(MeshDrawData);

    auto meshDrawDataBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
        sizeofMeshDrawDataBuffer,
        "MeshDrawDataBuffer");

    void* meshDrawDataMapped = meshDrawDataBuffer.GetMappedRange();
    MLG_CHECK(meshDrawDataMapped, "Failed to map MeshDrawDataBuffer");

    MeshDrawData* meshDrawData = reinterpret_cast<MeshDrawData*>(meshDrawDataMapped);

    uint32_t meshCount = 0;

    for(const auto& modelInstance : modelInstances)
    {
        std::span<const MeshData> meshes //
            {
                meshDatas.data() + modelInstance.FirstMesh,
                modelInstance.MeshCount,
            };

        for(const auto& meshData : meshes)
        {
            meshDrawData[meshCount] = //
            {
                .TransformIndex = modelInstance.TransformIndex,
                .MaterialIndex = meshData.MaterialIndex,
            };

            ++meshCount;
        }
    }

    meshDrawDataBuffer.Unmap();

    return meshDrawDataBuffer;
}

Result<SceneKit*>
CgltfModelLoader::LoadSceneKit(wgpu::Device& wgpuDevice, const std::string& path)
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

    SceneKitData sceneKitData;
    {
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
    }

    auto textureCache = CreateTextureCache(wgpuDevice);
    MLG_CHECK(textureCache);

    MLG_CHECK(FetchTextures(wgpuDevice, filePath.parent_path(), sceneKitData.Materials, *textureCache));

    auto vertexBuffer = BuildVertexBuffer(wgpuDevice, sceneKitData.Vertices);
    MLG_CHECK(vertexBuffer);

    auto indexBuffer = BuildIndexBuffer(wgpuDevice, sceneKitData.Indices);
    MLG_CHECK(indexBuffer);

    auto transformBuffer = BuildTransformBuffer(wgpuDevice, sceneKitData.Transforms);
    MLG_CHECK(transformBuffer);

    auto materialConstantsBuffer = BuildMaterialConstantsBuffer(wgpuDevice, sceneKitData.Materials);
    MLG_CHECK(materialConstantsBuffer);

    auto drawIndirectBuffer = BuildDrawIndirectBuffer(wgpuDevice, sceneKitData.Meshes, sceneKitData.ModelInstances);
    MLG_CHECK(drawIndirectBuffer);

    auto meshDrawDataBuffer = BuildMeshDrawDataBuffer(wgpuDevice, sceneKitData.Meshes, sceneKitData.ModelInstances);
    MLG_CHECK(meshDrawDataBuffer);

    std::vector<wgpu::BindGroup> materialBindGroups;
    MLG_CHECK(CreateMaterialBindGroups(wgpuDevice, sceneKitData.Materials, *textureCache, materialBindGroups));

    ColorPipelineResources colorPipelineResources //
    {
        .TransformBuffer = *transformBuffer,
        .MaterialConstantsBuffer = *materialConstantsBuffer,
        .MeshDrawDataBuffer = *meshDrawDataBuffer,
    };

    auto colorPipelineBindGroup0 = CreateColorPipelineBindGroup0(wgpuDevice, colorPipelineResources);
    MLG_CHECK(colorPipelineBindGroup0);

    TransformPipelineResources transformPipelineResources //
    {
        .TransformBuffer = *transformBuffer,
    };

    auto transformPipelineBindGroup0 = CreateTransformPipelineBindGroup0(wgpuDevice, transformPipelineResources);
    MLG_CHECK(transformPipelineBindGroup0);

    cgltf_free(gltfData);

    std::vector<MeshProperties> meshProperties;
    meshProperties.reserve(sceneKitData.Meshes.size());
    for(const auto& meshData : sceneKitData.Meshes)
    {
        const MeshProperties mesh //
        {
            .MaterialIndex = meshData.MaterialIndex,
        };

        meshProperties.emplace_back(std::move(mesh));
    }

    DawnSceneKit::Builder builder;

    builder.SetIndexBuffer(*indexBuffer)
        .SetVertexBuffer(*vertexBuffer)
        .SetTransformBuffer(*transformBuffer)
        .SetMaterialConstantsBuffer(*materialConstantsBuffer)
        .SetDrawIndirectBuffer(*drawIndirectBuffer)
        .SetMeshDrawDataBuffer(*meshDrawDataBuffer)
        .SetColorPipelineBindGroup0(*colorPipelineBindGroup0)
        .SetTransformPipelineBindGroup0(*transformPipelineBindGroup0)
        .SetMaterialBindGroups(std::move(materialBindGroups))
        .SetMeshes(std::move(meshProperties))
        .SetModelInstances(std::move(sceneKitData.ModelInstances));

    DawnSceneKit* sceneKit = builder.Build();

    return sceneKit;
}