#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#define __LOGGER_NAME__ "GLTF"

#include "CgltfModelLoader.h"

#include "DawnGpuDevice.h"
#include "FileFetcher.h"
#include "Log.h"
#include "scope_exit.h"
#include "ThreadPool.h"
#include "Vertex.h"

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
struct PrimitiveAttributes
{
    // Mesh that owns this primitive
    const cgltf_mesh* SrcMesh{ nullptr };
    // Index of this primitive within the mesh
    uint32_t IndexInMesh{ 0 };

    const cgltf_material* SrcMaterial{ nullptr };
    const cgltf_accessor* SrcIndices{ nullptr };
    const cgltf_accessor* SrcPosition{ nullptr };
    const cgltf_accessor* SrcNormal{ nullptr };
    const cgltf_accessor* SrcTexcoord0{ nullptr };
    const cgltf_accessor* SrcTexcoord1{ nullptr };

    const char* GetMeshName() const { return SrcMesh->name ? SrcMesh->name : "<unnamed>"; }

    const cgltf_primitive* GetPrim() const { return &SrcMesh->primitives[IndexInMesh]; }
};

struct PrimitiveInstance
{
    size_t NodeIndex{ 0 };
    size_t AttrsIndex{ 0 };
};

struct MeshDrawParams
{
    uint32_t IndexCount{ 0 };
    uint32_t InstanceCount{ 0 };
    uint32_t FirstIndex{ 0 };
    uint32_t BaseVertex{ 0 };
    uint32_t FirstInstance{ 0 };
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

struct MaterialResources
{
    wgpu::Texture BaseColorTexture;
    wgpu::Sampler Sampler;
    wgpu::Buffer ConstantsBuffer;
    wgpu::BindGroup BindGroup;
};

struct MaterialData
{
    std::string BaseTextureUri;
    RgbaColorf Color;
    float Metalness;
    float Roughness;
};

struct MeshData
{
    uint32_t FirstIndex;
    uint32_t BaseVertex;
    uint32_t IndexCount;
    uint32_t MaterialIndex;
};

struct ModelData
{
    std::vector<MeshData> Meshes;
};

struct ModelInstance
{
    uint32_t ModelIndex;
    uint32_t TransformIndex;
};

struct TransformData
{
    Mat44f Transform;
    uint32_t ParentIndex;
};

struct SceneData
{
    std::vector<Vertex> Vertices;
    std::vector<VertexIndex> Indices;
    std::vector<MaterialData> Materials;
    std::vector<ModelData> Models;
    std::map<const cgltf_mesh*, size_t> ModelMap;
    std::vector<ModelInstance> Instances;
    std::vector<TransformData> Transforms;

    wgpu::Buffer IndexBuffer;
    wgpu::Buffer VertexBuffer;
    wgpu::Buffer TransformBuffer;
    wgpu::Buffer MeshDrawParamsBuffer;
    wgpu::Buffer MeshToTransformMapBuffer;

    std::vector<MaterialResources> MaterialRsrcs;

    std::map<std::string, wgpu::Texture> Textures;

    wgpu::Sampler DefaultSampler;

    wgpu::Texture DefaultTexture;

    wgpu::BindGroupLayout MaterialBindGroupLayout;
};
} // namespace

static Result<>
CollectPrimitiveAttributes(const cgltf_primitive& primitive, PrimitiveAttributes& attrs)
{
    MLG_CHECK(primitive.type == cgltf_primitive_type_triangles,
        "Only triangle primitives are supported.");

    MLG_CHECK(primitive.material, "Primitive does not have a material. Ignoring.");

    MLG_CHECK(primitive.attributes_count > 0, "Primitive does not have any attributes. Ignoring.");

    MLG_CHECK(primitive.targets_count == 0, "Morph targets are not supported. Ignoring.");

    MLG_CHECK(!primitive.has_draco_mesh_compression,
        "Draco mesh compression is not supported. Ignoring.");

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

    return Result<>::Ok;
}

static Result<size_t>
CollectVertices(const PrimitiveAttributes& attrs, std::vector<Vertex>& vertices)
{
    for(cgltf_size i = 0; i < attrs.SrcPosition->count; ++i)
    {
        Vertex vertex;
        MLG_CHECK(cgltf_accessor_read_float(attrs.SrcPosition, i, &vertex.pos.x, 3),
            "Failed to read POSITION attribute");

        // Convert from right handed to left handed.
        vertex.pos.z = -vertex.pos.z;

        if(attrs.SrcNormal)
        {
            MLG_CHECK(cgltf_accessor_read_float(attrs.SrcNormal, i, &vertex.normal.x, 3),
                "Failed to read NORMAL attribute");
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
CollectIndices(const PrimitiveAttributes& attrs, std::vector<VertexIndex>& indices)
{
    if(attrs.SrcIndices)
    {
        const size_t startIndex = indices.size();
        indices.resize(indices.size() + attrs.SrcIndices->count);
        VertexIndex* idxDst = indices.data() + startIndex;

        const size_t count = cgltf_accessor_unpack_indices(attrs.SrcIndices,
            idxDst,
            sizeof(uint32_t),
            attrs.SrcIndices->count);

        MLG_CHECK(count == attrs.SrcIndices->count,
            "Failed to unpack all indices for primitive");

        return attrs.SrcIndices->count;
    }
    else
    {
        // Non-indexed primitive.

        const size_t startIndex = indices.size();
        indices.resize(indices.size() + attrs.SrcPosition->count);

        VertexIndex* idxDst = indices.data() + startIndex;
        for(size_t i = 0; i < attrs.SrcPosition->count; ++i)
        {
            idxDst[i] = static_cast<VertexIndex>(i);
        }

        return attrs.SrcPosition->count;
    }
}

static Result<>
GenerateNormals(
    const PrimitiveAttributes& primAttrs, Vertex* vertices, const VertexIndex* indices)
{
    if(primAttrs.SrcNormal)
    {
        // Already has normals, skip generating them.

        return Result<>::Ok;
    }

    MLG_LOG_SCOPE("mesh {}", primAttrs.GetMeshName());
    MLG_LOG_SCOPE("prim {}", primAttrs.IndexInMesh);

    const size_t idxCount =
        primAttrs.SrcIndices ? primAttrs.SrcIndices->count : primAttrs.SrcPosition->count;

    for(size_t i = 0; i < primAttrs.SrcPosition->count; ++i)
    {
        vertices[i].normal = { 0.0f, 0.0f, 0.0f };
    }

    for(size_t i = 0; i < idxCount; i += 3)
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

    for(size_t i = 0; i < primAttrs.SrcPosition->count; ++i)
    {
        vertices[i].normal = vertices[i].normal.Normalize();
    }

    return Result<>::Ok;
}

static Result<>
CollectPrimitives(const cgltf_data* gltfData, SceneData& sceneData)
{
    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;
    std::vector<MaterialData> materials;
    std::map<const cgltf_material*, size_t> materialMap;
    std::map<const cgltf_mesh*, size_t> modelMap;
    std::vector<ModelData> models;

    uint32_t firstIndex = 0;
    uint32_t baseVertex = 0;

    for(cgltf_size i = 0; i < gltfData->meshes_count; ++i)
    {
        const cgltf_mesh* mesh = &gltfData->meshes[i];

        MLG_LOG_SCOPE("mesh {}/{}", mesh->name ? mesh->name : "<unnamed>", i);

        ModelData model;

        for(cgltf_size j = 0; j < mesh->primitives_count; ++j)
        {
            MLG_LOG_SCOPE("prim {}", j);

            const cgltf_primitive* prim = &mesh->primitives[j];

            if(!prim->material)
            {
                MLG_WARN("Primitive has no material.  Ignoring");
                continue;
            }

            PrimitiveAttributes attrs //
            {
                .SrcMesh = mesh,
                .IndexInMesh = static_cast<uint32_t>(j),
                .SrcMaterial = prim->material,
                .SrcIndices = prim->indices,
            };

            if(!CollectPrimitiveAttributes(*prim, attrs))
            {
                continue;
            }

            auto vertexCount = CollectVertices(attrs, vertices);
            MLG_CHECK(vertexCount);

            auto indexCount = CollectIndices(attrs, indices);
            MLG_CHECK(indexCount);

            GenerateNormals(attrs, vertices.data() + baseVertex, indices.data() + firstIndex);

            // Change winding from CCW to CW.

            const size_t endIndex = *indexCount + firstIndex;
            for(size_t idx = firstIndex; idx < endIndex; idx += 3)
            {
                std::swap(indices[idx + 1], indices[idx + 2]);
            }

            size_t materialIndex;

            if(materialMap.contains(prim->material))
            {
                materialIndex = materialMap[prim->material];
            }
            else
            {
                std::string baseTextureUri;
                RgbaColorf color{"#FF00FFFF"_rgba};
                float metalness = 0;
                float roughness = 0;

                if(prim->material->has_pbr_metallic_roughness)
                {
                    const cgltf_pbr_metallic_roughness& pbr =
                        prim->material->pbr_metallic_roughness;
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
                materialIndex = materials.size();
                materials.push_back(materialData);
            }

            materialMap[prim->material] = materialIndex;

            MeshData meshData //
            {
                .FirstIndex = static_cast<uint32_t>(firstIndex),
                .BaseVertex = static_cast<uint32_t>(baseVertex),
                .IndexCount = static_cast<uint32_t>(*indexCount),
                .MaterialIndex = static_cast<uint32_t>(materialIndex),
            };

            model.Meshes.push_back(meshData);

            firstIndex += static_cast<uint32_t>(indexCount);
            baseVertex += static_cast<uint32_t>(vertexCount);
        }

        modelMap[mesh] = models.size();
        models.push_back(std::move(model));
    }

    sceneData.Vertices = std::move(vertices);
    sceneData.Indices = std::move(indices);
    sceneData.Materials = std::move(materials);
    sceneData.Models = std::move(models);
    sceneData.ModelMap = std::move(modelMap);

    return Result<>::Ok;
}

static void
ConvertRHtoLH(Mat44f& M)
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

// Collect nodes.
static Result<size_t>
CollectTransforms(cgltf_node** const childNodes,
    const cgltf_size nodeCount,
    const uint32_t parentIndex,
    SceneData& sceneData)
{
    const size_t numTransforms = sceneData.Transforms.size();

    for(cgltf_size i = 0; i < nodeCount; ++i)
    {
        const cgltf_node* srcNode = childNodes[i];

        MLG_LOG_SCOPE("node {}", srcNode->name ? srcNode->name : "<unnamed>");

        const uint32_t transformIndex = static_cast<uint32_t>(sceneData.Transforms.size());

        TransformData transformData //
        {
            .ParentIndex = parentIndex
        };

        cgltf_node_transform_world(srcNode, &transformData.Transform.m[0].x);
        ConvertRHtoLH(transformData.Transform);

        sceneData.Transforms.emplace_back(transformData);

        auto countResult = CollectTransforms(srcNode->children,
            srcNode->children_count,
            transformIndex,
            sceneData);

        if(!countResult)
        {
            // Something went wrong. Erase the transform we added.
            sceneData.Transforms.resize(transformIndex);
        }
        else if(*countResult == 0 && !srcNode->mesh)
        {
            // No child transforms were added and there's no mesh
            // at this node.  Erase the transform we added.
            sceneData.Transforms.resize(transformIndex);

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
        else if(!sceneData.ModelMap.contains(srcNode->mesh))
        {
            // This mesh was rejected during the model collection phase.
            // No instance to add.
            continue;
        }
        else
        {
            ModelInstance instance //
            {
                .ModelIndex = static_cast<uint32_t>(sceneData.ModelMap[srcNode->mesh]),
                .TransformIndex = static_cast<uint32_t>(transformIndex),
            };

            sceneData.Instances.push_back(instance);
        }
    }

    const size_t numAdded = sceneData.Transforms.size() - numTransforms;

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
           static_cast<int>(request.Data.size()),
           &width,
           &height,
           &numChannels))
    {
        MLG_ERROR("Error getting image info - {}", uri, stbi_failure_reason());
        return Result<>::Fail;
    }

    MLG_DEBUG("Image info - {} x {} x {}", width, height, numChannels);

    auto [stagingBuffer, texture] = CreateTexture(wgpuDevice,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        kTextureFormat,
        uri);

    void* mappedMemory = stagingBuffer.GetMappedRange();
    MLG_CHECK(mappedMemory, "Failed to map staging buffer for texture upload");

    auto [it, inserted] = textureBuilders.try_emplace(uri,
        uri,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        static_cast<uint32_t>(numChannels),
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
            static_cast<int>(builder.Request->Data.size()),
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
FetchTextures(wgpu::Device wgpuDevice, std::filesystem::path basePath, SceneData& sceneData)
{
    std::map<std::string, FileFetcher::Request> fetchRequests;

    for(const auto& mtl : sceneData.Materials)
    {
        if(mtl.BaseTextureUri.empty())
        {
            continue;
        }

        if(fetchRequests.contains(mtl.BaseTextureUri))
        {
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

    struct QueueSubmitResult
    {
        std::atomic<bool> done = false;
        Result<> queueSubmitResult = Result<>::Ok;
    };

    QueueSubmitResult result;

    // TODO - change API to separate creating a resource from populating it
    wgpuDevice.GetQueue().Submit(1, &commandBuffer);
    wgpuDevice.GetQueue().OnSubmittedWorkDone(
        wgpu::CallbackMode::AllowProcessEvents,
        +[](wgpu::QueueWorkDoneStatus status, wgpu::StringView message, QueueSubmitResult* result)
        {
            if(status != wgpu::QueueWorkDoneStatus::Success)
            {
                MLG_ERROR("Queue submit failed: {}", std::string(message.data, message.length));
                result->queueSubmitResult = Result<>::Fail;
            }
            result->done.store(true);
        },
        &result);

    while(!result.done.load())
    {
        wgpuDevice.GetAdapter().GetInstance().ProcessEvents();
    }

    std::map<std::string, wgpu::Texture> textures;

    for(auto& [uri, builder] : textureBuilders)
    {
        textures[uri] = builder.Texture;
    }

    sceneData.Textures = std::move(textures);

    return Result<>::Ok;
}

static Result<>
CreateMaterialDefaults(wgpu::Device wgpuDevice, SceneData& sceneData)
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

    wgpu::BindGroupLayoutEntry bglEntries[] //
        {
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Fragment,
                .texture //
                {
                    .sampleType = wgpu::TextureSampleType::Float,
                    .viewDimension = wgpu::TextureViewDimension::e2D,
                    .multisampled = false,
                },
            },
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Fragment,
                .sampler //
                {
                    .type = wgpu::SamplerBindingType::Filtering,
                },
            },
            {
                .binding = 2,
                .visibility = wgpu::ShaderStage::Fragment,
                .buffer //
                {
                    .type = wgpu::BufferBindingType::Uniform,
                },
            },
        };

    wgpu::BindGroupLayoutDescriptor bglDesc//
    {
        .label = "MaterialBindGroupLayout",
        .entryCount = std::size(bglEntries),
        .entries = bglEntries,
    };

    wgpu::BindGroupLayout materialBindGroupLayout = wgpuDevice.CreateBindGroupLayout(&bglDesc);

    sceneData.DefaultTexture = defaultTexture;
    sceneData.DefaultSampler = defaultSampler;
    sceneData.MaterialBindGroupLayout = materialBindGroupLayout;

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
BuildMaterials(wgpu::Device wgpuDevice, SceneData& sceneData)
{
    MLG_CHECK(CreateMaterialDefaults(wgpuDevice, sceneData));

    std::vector<MaterialResources> materials;

    for(const auto& mtl : sceneData.Materials)
    {
        wgpu::Texture baseTexture = mtl.BaseTextureUri.empty()
                                        ? sceneData.DefaultTexture
                                        : sceneData.Textures[mtl.BaseTextureUri];

        wgpu::Buffer materialConstantsBuffer = CreateGpuBuffer(wgpuDevice,
            wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            sizeof(MaterialConstants),
            "MaterialConstants");

        materialConstantsBuffer.Unmap();

        MaterialConstants materialConstants //
            {
                mtl.Color,
                mtl.Metalness,
                mtl.Roughness
            };

        wgpuDevice.GetQueue().WriteBuffer(materialConstantsBuffer,
            0,
            &materialConstants,
            sizeof(MaterialConstants));

        wgpu::BindGroupEntry bgEntries[3]//
        {
            {
                .binding = 0,
                .textureView = baseTexture.CreateView(),
            },
            {
                .binding = 1,
                .sampler = sceneData.DefaultSampler,
            },
            {
                .binding = 2,
                .buffer = materialConstantsBuffer,
                .offset = 0,
                .size = sizeof(MaterialConstants),
            },
        };

        wgpu::BindGroupDescriptor bindGroupDesc //
        {
            .label = "MaterialBindGroup",
            .layout = sceneData.MaterialBindGroupLayout,
            .entryCount = std::size(bgEntries),
            .entries = bgEntries,
        };

        wgpu::BindGroup materialBindGroup = wgpuDevice.CreateBindGroup(&bindGroupDesc);

        materials.emplace_back(baseTexture,
            sceneData.DefaultSampler,
            materialConstantsBuffer,
            materialBindGroup);
    }

    sceneData.MaterialRsrcs = std::move(materials);

    return Result<>::Ok;
}

static Result<>
BuildVertexBuffer(wgpu::Device wgpuDevice, SceneData& sceneData)
{
    const size_t sizeofBuffer = sceneData.Vertices.size() * sizeof(Vertex);
    wgpu::Buffer vertexBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst,
        sizeofBuffer,
        "VertexBuffer");

    void* vbMapped = vertexBuffer.GetMappedRange();
    MLG_CHECK(vbMapped, "Failed to map VertexBuffer");

    ::memcpy(vbMapped, sceneData.Vertices.data(), sizeofBuffer);

    vertexBuffer.Unmap();

    sceneData.VertexBuffer = vertexBuffer;

    return Result<>::Ok;
}

static Result<>
BuildIndexBuffer(wgpu::Device wgpuDevice, SceneData& sceneData)
{
    const size_t sizeofBuffer = sceneData.Indices.size() * sizeof(VertexIndex);
    wgpu::Buffer indexBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst,
        sizeofBuffer,
        "IndexBuffer");

    void* ibMapped = indexBuffer.GetMappedRange();
    MLG_CHECK(ibMapped, "Failed to map IndexBuffer");

    ::memcpy(ibMapped, sceneData.Indices.data(), sizeofBuffer);

    indexBuffer.Unmap();

    sceneData.IndexBuffer = indexBuffer;

    return Result<>::Ok;
}

static Result<>
BuildTransformBuffer(wgpu::Device wgpuDevice, SceneData& sceneData)
{
    const size_t sizeofBuffer = sceneData.Transforms.size() * sizeof(Mat44f);

    wgpu::Buffer transformBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
        sizeofBuffer,
        "TransformBuffer");

    void* mappedRange = transformBuffer.GetMappedRange();
    MLG_CHECK(mappedRange, "Failed to map TransformBuffer");

    auto cleanup = scope_exit([&]() { transformBuffer.Unmap(); });

    Mat44f* dst = reinterpret_cast<Mat44f*>(mappedRange);

    for(const TransformData& transform : sceneData.Transforms)
    {
        *dst++ = transform.Transform;
    }

    sceneData.TransformBuffer = transformBuffer;

    return Result<>::Ok;
}

static Result<>
BuildDrawBuffers(wgpu::Device wgpuDevice, SceneData& sceneData)
{
    size_t meshCount = 0;
    for(const auto& instance : sceneData.Instances)
    {
        const auto& model = sceneData.Models[instance.ModelIndex];
        meshCount += model.Meshes.size();
    }

    const size_t sizeofMeshDrawParamsBuffer = meshCount * sizeof(MeshDrawParams);
    const size_t sizeofMeshToTransformMapBuffer = meshCount * sizeof(uint32_t);

    sceneData.MeshDrawParamsBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
        sizeofMeshDrawParamsBuffer,
        "MeshDrawParamsBuffer");

    sceneData.MeshToTransformMapBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
        sizeofMeshToTransformMapBuffer,
        "MeshToTransformMapBuffer");

    void* dpMapped = sceneData.MeshDrawParamsBuffer.GetMappedRange();
    MLG_CHECK(dpMapped, "Failed to map MeshDrawParamsBuffer");

    auto cleanupDrawParams = scope_exit([&]() { sceneData.MeshDrawParamsBuffer.Unmap(); });

    void* mtMapped =
        sceneData.MeshToTransformMapBuffer.GetMappedRange();
    MLG_CHECK(mtMapped, "Failed to map MeshToTransformMapBuffer");

    auto cleanupMap = scope_exit([&]() { sceneData.MeshToTransformMapBuffer.Unmap(); });

    MeshDrawParams* drawParams = reinterpret_cast<MeshDrawParams*>(dpMapped);
    uint32_t* meshToTransformMap = reinterpret_cast<uint32_t*>(mtMapped);

    for(const auto& instance : sceneData.Instances)
    {
        for(const auto& mesh : sceneData.Models[instance.ModelIndex].Meshes)
        {
            *drawParams++ = //
            {
                .IndexCount = mesh.IndexCount,
                .InstanceCount = 1,
                .FirstIndex = mesh.FirstIndex,
                .BaseVertex = mesh.BaseVertex,
                .FirstInstance = 0
            };

            *meshToTransformMap++ = instance.TransformIndex;
        }
    }

    return Result<>::Ok;
}

Result<>
CgltfModelLoader::LoadModel(GpuDevice* gpuDevice, const std::string& path)
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

    SceneData sceneData;
    wgpu::Device wgpuDevice = static_cast<DawnGpuDevice*>(gpuDevice)->Device;

    MLG_CHECK(CollectPrimitives(data, sceneData));
    MLG_CHECK(CollectTransforms(data->scenes[0].nodes,
        data->scenes[0].nodes_count,
        std::numeric_limits<uint32_t>::max(),
        sceneData));

    MLG_CHECK(FetchTextures(wgpuDevice, filePath.parent_path(), sceneData));

    MLG_CHECK(BuildMaterials(wgpuDevice, sceneData));

    MLG_CHECK(BuildVertexBuffer(wgpuDevice, sceneData));
    MLG_CHECK(BuildIndexBuffer(wgpuDevice, sceneData));

    MLG_CHECK(BuildTransformBuffer(wgpuDevice, sceneData));
    MLG_CHECK(BuildDrawBuffers(wgpuDevice, sceneData));

    cgltf_free(data);

    return Result<>::Ok;
}