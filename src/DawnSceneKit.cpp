#define _CRT_SECURE_NO_WARNINGS
#define NOMINMAX

#define __LOGGER_NAME__ "SCKT"

#include "DawnSceneKit.h"
#include "FileFetcher.h"
#include "Log.h"
#include "Material.h"
#include "ThreadPool.h"
#include "WebgpuHelper.h"

#include <atomic>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

static constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::RGBA8Unorm;
static constexpr int kNumTextureChannels = 4;

namespace
{
struct TextureBuilder
{
    std::string Uri;
    const FileFetcher::Request* Request{ nullptr };
    wgpu::Buffer StagingBuffer;
    wgpu::Texture Texture;
    void* MappedMemory{ nullptr };
    std::atomic<bool> DecodeComplete{ false };
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
    std::span<const MaterialData> materials,
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
BuildVertexBuffer(wgpu::Device wgpuDevice, std::span<const Vertex> vertices)
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
BuildIndexBuffer(wgpu::Device wgpuDevice, std::span<const VertexIndex> indices)
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
BuildTransformBuffer(wgpu::Device wgpuDevice, std::span<const TransformData> transforms)
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
    wgpu::Device wgpuDevice, std::span<const MaterialData> materials)
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

Result<DawnSceneKit*>
DawnSceneKit::Create(wgpu::Device& wgpuDevice,
    const std::filesystem::path& rootPath,
    const SceneKitSourceData& sceneKitData)
{
    auto textureCache = CreateTextureCache(wgpuDevice);
    MLG_CHECK(textureCache);

    MLG_CHECK(FetchTextures(wgpuDevice, rootPath, sceneKitData.Materials, *textureCache));

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

    std::vector<ModelInstance> modelInstances(sceneKitData.ModelInstances);

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
        .SetModelInstances(std::move(modelInstances));

    DawnSceneKit* sceneKit = builder.Build();

    return sceneKit;
}

void
DawnSceneKit::Destroy(DawnSceneKit* sceneKit)
{
    delete sceneKit;
}