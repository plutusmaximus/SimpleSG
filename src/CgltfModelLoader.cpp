#define _CRT_SECURE_NO_WARNINGS

#define __LOGGER_NAME__ "GLTF"

#include "CgltfModelLoader.h"

#include "DawnGpuDevice.h"
#include "FileFetcher.h"
#include "Log.h"
#include "scope_exit.h"
#include "ThreadPool.h"
#include "Vertex.h"

#define CGLTF_IMPLEMENTATION
#include <cgltf.h>

#include <webgpu/webgpu_cpp.h>

#include <stb_image.h>

#include <atomic>
#include <filesystem>
#include <map>
#include <vector>

static constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::RGBA8Unorm;
static constexpr int kNumTextureChannels = 4;

namespace
{
struct PrimitiveAttributes
{
    // Mesh that owns this primitive
    const cgltf_mesh* SrcMesh{nullptr};
    // Index of this primitive within the mesh
    uint32_t IndexInMesh{0};

    const cgltf_material* SrcMaterial{nullptr};
    const cgltf_accessor* SrcIndices{nullptr};
    const cgltf_accessor* SrcPosition{nullptr};
    const cgltf_accessor* SrcNormal{nullptr};
    const cgltf_accessor* SrcTexcoord0{nullptr};
    const cgltf_accessor* SrcTexcoord1{nullptr};

    // First index in the GPU index buffer for this primitive
    uint32_t IbFirstIndex{0};
    // Base vertex in the GPU vertex buffer for this primitive
    uint32_t VbBaseVertex{0};

    const char* GetMeshName() const { return SrcMesh->name ? SrcMesh->name : "<unnamed>"; }
};

struct Node
{
    size_t ParentIndex{0};
    const cgltf_node* NodePtr{nullptr};
};

struct PrimitiveInstance
{
    size_t NodeIndex{0};
    const PrimitiveAttributes* Attrs{nullptr};
};

struct MeshDrawParams
{
    uint32_t IndexCount{0};
    uint32_t InstanceCount{0};
    uint32_t FirstIndex{0};
    uint32_t BaseVertex{0};
    uint32_t FirstInstance{0};
};

struct Material
{
};

struct SceneData
{
    std::vector<Node> Nodes;
    std::vector<PrimitiveInstance> Instances;
    std::map<const cgltf_primitive*, PrimitiveAttributes> Attrs;


    wgpu::Buffer IndexBuffer;
    wgpu::Buffer VertexBuffer;
    wgpu::Buffer TransformBuffer;
    wgpu::Buffer MeshDrawParamsBuffer;
    wgpu::Buffer MeshToTransformMapBuffer;

    std::map<std::string, wgpu::Texture> Textures;
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
        .SrcMaterial = primitive.material,
        .SrcIndices = primitive.indices,
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

    if(!attrs.SrcPosition || attrs.SrcPosition->count == 0)
    {
        MLG_WARN("Primitive does not have a POSITION attribute.  Ignoring.");
        return Result<>::Fail;
    }

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
                if(!sceneData.Attrs.contains(prim))
                {
                    Result<PrimitiveAttributes> attrs = CollectPrimitiveAttributes(*prim);
                    if(!attrs)
                    {
                        continue;
                    }

                    attrs->SrcMesh = srcNode->mesh;
                    attrs->IndexInMesh = static_cast<uint32_t>(j);
                    sceneData.Attrs[prim] = *attrs;
                }

                const PrimitiveInstance instance //
                    {
                        .NodeIndex = sceneData.Nodes.size(),
                        .Attrs = &sceneData.Attrs[prim],
                    };

                sceneData.Instances.emplace_back(instance);
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

namespace
{
struct TextureBuilder
{
    std::string Uri;
    uint32_t Width;
    uint32_t Height;
    uint32_t NumChannels;
    uint32_t AlignedRowPitch;
    wgpu::Buffer StagingBuffer;
    wgpu::Texture Texture;
    void* MappedMemory{ nullptr };
    const FileFetcher::Request* Request{ nullptr };
    std::atomic<bool> DecodeComplete{ false };
};
} // namespace

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

    const uint32_t rowPitch = width * kNumTextureChannels;
    // Staging buffer rows must be a multiple of 256 bytes.
    const uint32_t alignedRowPitch = (rowPitch + 255) & ~255;
    const uint32_t stagingSize = alignedRowPitch * height;

    wgpu::BufferDescriptor bufDesc = //
        {
            .label = uri.c_str(),
            .usage = wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::MapWrite,
            .size = stagingSize,
            .mappedAtCreation = true,
        };

    wgpu::Buffer stagingBuffer = wgpuDevice.CreateBuffer(&bufDesc);

    wgpu::TextureDescriptor desc //
        {
            .label = uri.c_str(),
            .usage = wgpu::TextureUsage::TextureBinding | wgpu::TextureUsage::CopyDst,
            .dimension = wgpu::TextureDimension::e2D,
            .size = //
            {
                .width = static_cast<uint32_t>(width),
                .height = static_cast<uint32_t>(height),
                .depthOrArrayLayers = 1,
            },
            .format = kTextureFormat,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };

    wgpu::Texture texture = wgpuDevice.CreateTexture(&desc);

    void* mappedMemory = stagingBuffer.GetMappedRange(0, stagingSize);
    MLG_CHECK(mappedMemory, "Failed to map staging buffer for texture upload");

    auto [it, inserted] = textureBuilders.try_emplace(uri,
        uri,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height),
        static_cast<uint32_t>(numChannels),
        alignedRowPitch,
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
            uint8_t *dst = (uint8_t *)builder.MappedMemory;
            const uint8_t *src = data;
            const uint32_t rowStride = width * kNumTextureChannels;
            for(uint32_t y = 0; y < builder.Height;
                ++y, dst += builder.AlignedRowPitch, src += rowStride)
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

    wgpu::TexelCopyBufferInfo copySrc = //
        {
            .layout = //
            {
                .offset = 0,
                .bytesPerRow = builder.AlignedRowPitch,
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

    for(const auto&[prim, attrs] : sceneData.Attrs)
    {
        MLG_LOG_SCOPE("mesh {}", attrs.GetMeshName());
        MLG_LOG_SCOPE("prim {}", attrs.IndexInMesh);

        if(!attrs.SrcMaterial->pbr_metallic_roughness.base_color_texture.texture)
        {
            MLG_WARN("Primitive has no base color texture");
            continue;
        }

        if(!attrs.SrcMaterial->pbr_metallic_roughness.base_color_texture.texture->image)
        {
            MLG_WARN("Primitive has no base color texture image");
            continue;
        }

        if(!attrs.SrcMaterial->pbr_metallic_roughness.base_color_texture.texture->image->uri)
        {
            MLG_WARN("Primitive has no base color texture image URI");
            continue;
        }

        const std::string baseTexUri =
            attrs.SrcMaterial->pbr_metallic_roughness.base_color_texture.texture->image->uri;

        std::string metallicRoughnessTexUri;

        if(attrs.SrcMaterial->pbr_metallic_roughness.metallic_roughness_texture.texture
            && attrs.SrcMaterial->pbr_metallic_roughness.metallic_roughness_texture.texture->image
            && attrs.SrcMaterial->pbr_metallic_roughness.metallic_roughness_texture.texture->image->uri)
        {
            metallicRoughnessTexUri =
                attrs.SrcMaterial->pbr_metallic_roughness.metallic_roughness_texture.texture->image->uri;
        }

        if(!sceneData.Textures.contains(baseTexUri))
        {
            auto [btIt, btInserted] =
                fetchRequests.try_emplace(baseTexUri, (basePath / baseTexUri).string());

            if(btInserted)
            {
                MLG_LOG_SCOPE(baseTexUri);

                MLG_DEBUG("Fetching texture...");

                if(!FileFetcher::Fetch(btIt->second))
                {
                    MLG_WARN("Failed to fetch texture: {}", btIt->second.FilePath);
                }
            }
        }

        if(!metallicRoughnessTexUri.empty() && !sceneData.Textures.contains(metallicRoughnessTexUri))
        {
            auto [mrIt, mrInserted] = fetchRequests.try_emplace(metallicRoughnessTexUri,
                (basePath / metallicRoughnessTexUri).string());

            if(mrInserted)
            {
                MLG_LOG_SCOPE(metallicRoughnessTexUri);

                MLG_DEBUG("Fetching texture...");

                if(!FileFetcher::Fetch(mrIt->second))
                {
                    MLG_WARN("Failed to fetch texture: {}", mrIt->second.FilePath);
                }
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
    }while (pending);

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
    }while (pending);

    wgpu::CommandBuffer commandBuffer = encoder.Finish();

    struct QueueSubmitResult
    {
        std::atomic<bool> done = false;
        Result<> queueSubmitResult = Result<>::Ok;
    };

    QueueSubmitResult result;

    //TODO - change API to separate creating a resource from populating it
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

    for(auto& [uri, builder] : textureBuilders)
    {
        sceneData.Textures[uri] = builder.Texture;
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
GenerateNormals(SceneData& sceneData, Vertex* vertices, const VertexIndex* indices)
{
    for(const auto& [prim, attrs] : sceneData.Attrs)
    {
        if(attrs.SrcNormal)
        {
            // Already has normals, skip generating them.
            continue;
        }

        MLG_LOG_SCOPE("mesh {}", attrs.GetMeshName());
        MLG_LOG_SCOPE("prim {}", attrs.IndexInMesh);

        const size_t idxCount = attrs.SrcIndices ? attrs.SrcIndices->count : attrs.SrcPosition->count;

        for(size_t i = 0; i < attrs.SrcPosition->count; ++i)
        {
            vertices[i].normal = {0.0f, 0.0f, 0.0f};
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

        for(size_t i = 0; i < attrs.SrcPosition->count; ++i)
        {
            vertices[i].normal = vertices[i].normal.Normalize();
        }
    }

    return Result<>::Ok;
}

static Result<>
BuildIndexVertexBuffers(wgpu::Device wgpuDevice, SceneData& sceneData)
{
    // Compute size of vertex buffer.
    size_t vbBufferSize = 0;
    size_t ibBufferSize = 0;

    for(const auto& [prim, attrs] : sceneData.Attrs)
    {
        vbBufferSize += attrs.SrcPosition->count * sizeof(Vertex);

        if(attrs.SrcIndices)
        {
            ibBufferSize += attrs.SrcIndices->count * sizeof(VertexIndex);
        }
        else
        {
            // Non-indexed primitive.
            ibBufferSize += attrs.SrcPosition->count * sizeof(VertexIndex);
        }
    }

    sceneData.VertexBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst,
        vbBufferSize,
        "VertexBuffer");

    sceneData.IndexBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst,
        ibBufferSize,
        "IndexBuffer");

    void* vbMapped = sceneData.VertexBuffer.GetMappedRange(0, vbBufferSize);
    MLG_CHECK(vbMapped, "Failed to map VertexBuffer");

    void* ibMapped = sceneData.IndexBuffer.GetMappedRange(0, ibBufferSize);
    MLG_CHECK(ibMapped, "Failed to map IndexBuffer");

    auto cleanupVb = scope_exit([&]() { sceneData.VertexBuffer.Unmap(); });
    auto cleanupIb = scope_exit([&]() { sceneData.IndexBuffer.Unmap(); });

    Vertex* vtxDst = reinterpret_cast<Vertex*>(vbMapped);
    VertexIndex* idxDst = reinterpret_cast<VertexIndex*>(ibMapped);

    uint32_t baseVertex = 0;
    uint32_t baseIndex = 0;

    for(auto& [prim, attrs] : sceneData.Attrs)
    {
        MLG_LOG_SCOPE("mesh {}", attrs.GetMeshName());
        MLG_LOG_SCOPE("prim {}", attrs.IndexInMesh);

        attrs.VbBaseVertex = baseVertex;
        attrs.IbFirstIndex = baseIndex;

        for(cgltf_size i = 0; i < attrs.SrcPosition->count; ++i, ++vtxDst, ++baseVertex)
        {
            MLG_CHECK(cgltf_accessor_read_float(attrs.SrcPosition, i, &vtxDst->pos.x, 3),
                "Failed to read POSITION attribute");

            // Convert from right handed to left handed.
            vtxDst->pos.z = -vtxDst->pos.z;

            if(attrs.SrcNormal)
            {
                MLG_CHECK(cgltf_accessor_read_float(attrs.SrcNormal, i, &vtxDst->normal.x, 3),
                    "Failed to read NORMAL attribute");
            }

            if(attrs.SrcTexcoord0)
            {
                MLG_CHECK(cgltf_accessor_read_float(attrs.SrcTexcoord0, i, &vtxDst->uvs[0].u, 2),
                    "Failed to read TEXCOORD_0 attribute");
            }
            else
            {
                vtxDst->uvs[0].u = 0.0f;
                vtxDst->uvs[0].v = 0.0f;
            }

            if(attrs.SrcTexcoord1)
            {
                MLG_CHECK(cgltf_accessor_read_float(attrs.SrcTexcoord1, i, &vtxDst->uvs[1].u, 2),
                    "Failed to read TEXCOORD_1 attribute");
            }
            else
            {
                vtxDst->uvs[1].u = 0.0f;
                vtxDst->uvs[1].v = 0.0f;
            }
        }

        if(attrs.SrcIndices)
        {
            const size_t count = cgltf_accessor_unpack_indices(attrs.SrcIndices,
                idxDst,
                sizeof(uint32_t),
                attrs.SrcIndices->count);

            MLG_CHECK(count == attrs.SrcIndices->count,
                "Failed to unpack all indices for primitive");

            baseIndex += static_cast<uint32_t>(attrs.SrcIndices->count);
            idxDst += attrs.SrcIndices->count;
        }
        else
        {
            // Non-indexed primitive.

            for(size_t i = 0; i < attrs.SrcPosition->count; ++i)
            {
                idxDst[i] = static_cast<VertexIndex>(i);
            }

            baseIndex += static_cast<uint32_t>(attrs.SrcPosition->count);
            idxDst += attrs.SrcPosition->count;
        }
    }

    // Change winding from CCW to CW.

    idxDst = reinterpret_cast<VertexIndex*>(ibMapped);

    const size_t indexCount = ibBufferSize / sizeof(VertexIndex);

    for(size_t i = 0; i < indexCount; i += 3)
    {
        std::swap(idxDst[i + 1], idxDst[i + 2]);
    }

    // Generate normals for those primitives that don't have them.
    GenerateNormals(sceneData,
        reinterpret_cast<Vertex*>(vbMapped),
        reinterpret_cast<VertexIndex*>(ibMapped));

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
BuildTransformBuffer(wgpu::Device wgpuDevice, SceneData& sceneData)
{
    const size_t sizeofBuffer = sceneData.Nodes.size() * sizeof(Mat44f);

    sceneData.TransformBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
        sizeofBuffer,
        "TransformBuffer");

    void* mappedRange = sceneData.TransformBuffer.GetMappedRange(0,sizeofBuffer);
    MLG_CHECK(mappedRange, "Failed to map TransformBuffer");

    auto cleanup = scope_exit([&]() { sceneData.TransformBuffer.Unmap(); });

    Mat44f* dst = reinterpret_cast<Mat44f*>(mappedRange);

    for(const Node& node : sceneData.Nodes)
    {
        cgltf_node_transform_local(node.NodePtr, &dst->m[0].x);
        ConvertRHtoLH(*dst);
        ++dst;
    }

    return Result<>::Ok;
}

static Result<>
BuildMeshBuffers(wgpu::Device wgpuDevice, SceneData& sceneData)
{
    const size_t sizeofMeshDrawParamsBuffer = sceneData.Instances.size() * sizeof(MeshDrawParams);
    const size_t sizeofMeshToTransformMapBuffer = sceneData.Instances.size() * sizeof(uint32_t);

    sceneData.MeshDrawParamsBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
        sizeofMeshDrawParamsBuffer,
        "MeshDrawParamsBuffer");

    sceneData.MeshToTransformMapBuffer = CreateGpuBuffer(wgpuDevice,
        wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
        sizeofMeshToTransformMapBuffer,
        "MeshToTransformMapBuffer");

    void* dpMapped = sceneData.MeshDrawParamsBuffer.GetMappedRange(0, sizeofMeshDrawParamsBuffer);
    MLG_CHECK(dpMapped, "Failed to map MeshDrawParamsBuffer");

    auto cleanupDrawParams = scope_exit([&]() { sceneData.MeshDrawParamsBuffer.Unmap(); });

    void* mtMapped = sceneData.MeshToTransformMapBuffer.GetMappedRange(0, sizeofMeshToTransformMapBuffer);
    MLG_CHECK(mtMapped, "Failed to map MeshToTransformMapBuffer");

    auto cleanupMap = scope_exit([&]() { sceneData.MeshToTransformMapBuffer.Unmap(); });

    MeshDrawParams* drawParams = reinterpret_cast<MeshDrawParams*>(dpMapped);
    uint32_t* meshToTransformMap = reinterpret_cast<uint32_t*>(mtMapped);


    for(size_t i = 0; i < sceneData.Instances.size(); ++i)
    {
        const PrimitiveInstance& instance = sceneData.Instances[i];
        const PrimitiveAttributes& attrs = *instance.Attrs;

        meshToTransformMap[i] = static_cast<uint32_t>(instance.NodeIndex);

        MeshDrawParams& params = drawParams[i];

        const size_t indexCount = attrs.SrcIndices ? attrs.SrcIndices->count : attrs.SrcPosition->count;

        params.IndexCount = static_cast<uint32_t>(indexCount);
        params.InstanceCount = 1;
        params.FirstIndex = attrs.IbFirstIndex;
        params.BaseVertex = attrs.VbBaseVertex;
        params.FirstInstance = static_cast<uint32_t>(i);
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

    wgpu::Device wgpuDevice = static_cast<DawnGpuDevice*>(gpuDevice)->Device;

    MLG_CHECK(BuildIndexVertexBuffers(wgpuDevice, sceneData));
    MLG_CHECK(BuildTransformBuffer(wgpuDevice, sceneData));
    MLG_CHECK(BuildMeshBuffers(wgpuDevice, sceneData));
    MLG_CHECK(FetchTextures(wgpuDevice, filePath.parent_path(), sceneData));

    cgltf_free(data);

    return Result<>::Ok;
}