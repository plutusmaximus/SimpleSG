#include "SDLGPUDevice.h"

#include "AutoDeleter.h"
#include "Image.h"
#include "SDLRenderGraph.h"

#include <SDL3/SDL.h>

//#define GPU_DRIVER_DIRECT3D
#define GPU_DRIVER_VULKAN

#if defined(GPU_DRIVER_DIRECT3D)

static const SDL_GPUShaderFormat SHADER_FORMAT = SDL_GPU_SHADERFORMAT_DXIL;
static const char* const DRIVER_NAME = "direct3d12";
static const char* const SHADER_EXTENSION = ".dxil";

#elif defined(GPU_DRIVER_VULKAN)

static const SDL_GPUShaderFormat SHADER_FORMAT = SDL_GPU_SHADERFORMAT_SPIRV;
static const char* const DRIVER_NAME = "vulkan";
static const char* const SHADER_EXTENSION = ".spv";

#else

#error Must define a GPU driver to use.

#endif

static constexpr std::hash<std::string_view> NAME_HASHER;

static std::expected<SDL_GPUBuffer*, Error> CreateGpuBuffer(
    SDL_GPUDevice* gpuDevice,
    const SDL_GPUBufferUsageFlags usageFlags,
    const void* bufferData,
    const unsigned sizeofBuffer);

static std::expected<void, Error> CopyToGpuBuffer(
    SDL_GPUDevice* gpuDevice,
    SDL_GPUBuffer* gpuBuffer,
    const void* data,
    const unsigned sizeofData);

static std::expected<SDL_GPUTexture*, std::string> CreateTexture(
    SDL_GPUDevice* gpuDevice,
    const unsigned width,
    const unsigned height,
    const uint8_t* pixels);

static std::expected<SDL_GPUShader*, Error> LoadShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    SDL_GPUShaderStage shaderStage,
    const unsigned numUniformBuffers,
    const unsigned numSamplers);

SDLVertexBuffer::~SDLVertexBuffer()
{
    if (m_Buffer) { SDL_ReleaseGPUBuffer(m_GpuDevice, m_Buffer); }
}

SDLIndexBuffer::~SDLIndexBuffer()
{
    if (m_Buffer) { SDL_ReleaseGPUBuffer(m_GpuDevice, m_Buffer); }
}

SDLGPUDevice::SDLGPUDevice(SDL_Window* window, SDL_GPUDevice* gpuDevice)
    : m_Window(window)
    , m_GpuDevice(gpuDevice)
{
}

std::expected<RefPtr<GPUDevice>, Error>
SDLGPUDevice::Create(SDL_Window* window)
{
    logInfo("Creating SDL GPU Device...");

    expect(SDL_SetHint(SDL_HINT_RENDER_VULKAN_DEBUG, "1"), SDL_GetError());

    // Initialize GPU device
    const bool debugMode = true;
    SDL_GPUDevice* gpuDevice = SDL_CreateGPUDevice(SHADER_FORMAT, debugMode, DRIVER_NAME);
    expect(gpuDevice, SDL_GetError());

    if (!SDL_ClaimWindowForGPUDevice(gpuDevice, window))
    {
        SDL_DestroyGPUDevice(gpuDevice);
        return std::unexpected(SDL_GetError());
    }

    return new SDLGPUDevice(window, gpuDevice);
}

SDLGPUDevice::~SDLGPUDevice()
{
    for (auto mtl : m_Materials)
    {
        delete mtl;
    }

    for (const auto& [_, pipeline] : m_PipelinesByKey)
    {
        SDL_ReleaseGPUGraphicsPipeline(m_GpuDevice, pipeline);
    }

    if (m_Sampler)
    {
        SDL_ReleaseGPUSampler(m_GpuDevice, m_Sampler);
    }

    for (const auto& [_, texRec] : m_TexturesByName)
    {
        SDL_ReleaseGPUTexture(m_GpuDevice, texRec.Texture);
    }

    for (const auto& [_, shaderRec] : m_VertexShadersByName)
    {
        SDL_ReleaseGPUShader(m_GpuDevice, shaderRec.Shader);
    }

    for (const auto& [_, shaderRec] : m_FragShadersByName)
    {
        SDL_ReleaseGPUShader(m_GpuDevice, shaderRec.Shader);
    }

    if (m_GpuDevice)
    {
        SDL_DestroyGPUDevice(m_GpuDevice);
    }
}

std::expected<RefPtr<Model>, Error>
SDLGPUDevice::CreateModel(const ModelSpec& modelSpec)
{
    auto vbResult = CreateVertexBuffer(modelSpec.Vertices);
    expect(vbResult, vbResult.error());
    auto vtxBuf = vbResult.value();

    auto ibResult = CreateIndexBuffer(modelSpec.Indices);
    expect(ibResult, ibResult.error());
    auto idxBuf = ibResult.value();

    std::vector<RefPtr<Mesh>> meshes;

    for (const auto& meshSpec : modelSpec.MeshSpecs)
    {
        if (!m_Sampler)
        {
            // Create sampler
            SDL_GPUSamplerCreateInfo samplerInfo =
            {
                .min_filter = SDL_GPU_FILTER_NEAREST,
                .mag_filter = SDL_GPU_FILTER_NEAREST,
                .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT
            };

            m_Sampler = SDL_CreateGPUSampler(m_GpuDevice, &samplerInfo);
            expect(m_Sampler, SDL_GetError());
        }

        //TODO - check material DB for existing material

        //TODO - texture DB
        auto albedoResult = GetOrCreateTextureFromPNG(meshSpec.MtlSpec.Albedo);
        expect(albedoResult, albedoResult.error());

        //FIXME - specify number of uniform buffers.
        auto vertexShaderResult = GetOrCreateVertexShader(meshSpec.MtlSpec.VertexShader, 3);
        expect(vertexShaderResult, vertexShaderResult.error());

        //FIXME - specify number of uniform samplers.
        auto fragShaderResult = GetOrCreateFragmentShader(meshSpec.MtlSpec.FragmentShader, 1);
        expect(fragShaderResult, fragShaderResult.error());

        SDLMaterial* mtl = new SDLMaterial(
            meshSpec.MtlSpec.Color,
            albedoResult.value(),
            m_Sampler,
            vertexShaderResult.value(),
            fragShaderResult.value());

        m_MaterialIndexById.emplace(mtl->Id, std::size(m_Materials));
        m_Materials.push_back(mtl);

        auto mesh = Mesh::Create(vtxBuf, idxBuf, meshSpec.IndexOffset, meshSpec.IndexCount, mtl->Id);

        meshes.push_back(mesh);
    }

    return Model::Create(meshes);
}

std::expected<RefPtr<RenderGraph>, Error>
SDLGPUDevice::CreateRenderGraph()
{
    return new SDLRenderGraph(this);
}

std::expected<const SDLMaterial*, Error>
SDLGPUDevice::GetMaterial(const MaterialId& mtlId) const
{
    auto it = m_MaterialIndexById.find(mtlId);
    if (m_MaterialIndexById.end() == it)
    {
        return std::unexpected("Invalid material ID");
    }

    return m_Materials[it->second];
}

std::expected<SDL_GPUShader*, Error>
SDLGPUDevice::GetOrCreateVertexShader(
    const std::string_view path,
    const int numUniformBuffers)
{
    SDL_GPUShader* shader = GetVertexShader(path);

    if (!shader)
    {
        auto shaderResult = LoadShader(m_GpuDevice, path, SDL_GPU_SHADERSTAGE_VERTEX, numUniformBuffers, 0);

        expect(shaderResult, shaderResult.error());

        shader = shaderResult.value();

        m_VertexShadersByName.emplace(NAME_HASHER(path), ShaderRecord{ .Name{path}, .Shader = shader });
    }

    return shader;
}

std::expected<SDL_GPUShader*, Error>
SDLGPUDevice::GetOrCreateFragmentShader(
    const std::string_view path,
    const int numSamplers)
{
    SDL_GPUShader* shader = GetFragShader(path);

    if (!shader)
    {
        auto shaderResult = LoadShader(m_GpuDevice, path, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, numSamplers);

        expect(shaderResult, shaderResult.error());

        shader = shaderResult.value();

        m_FragShadersByName.emplace(NAME_HASHER(path), ShaderRecord{ .Name{path}, .Shader = shader });
    }

    return shader;
}

std::expected<SDL_GPUGraphicsPipeline*, Error>
SDLGPUDevice::GetOrCreatePipeline(const SDLMaterial& mtl)
{
    SDL_GPUTextureFormat colorTargetFormat = SDL_GetGPUSwapchainTextureFormat(m_GpuDevice, m_Window);
    PipelineKey key
    {
        .ColorFormat = colorTargetFormat,
        .VertexShader = mtl.VertexShader,
        .FragShader = mtl.FragmentShader
    };

    auto it = m_PipelinesByKey.find(key);
    if (m_PipelinesByKey.end() != it)
    {
        return it->second;
    }

    SDL_GPUVertexBufferDescription vertexBufDescriptions[1] =
    {
        {
            .slot = 0,
            .pitch = sizeof(Vertex),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX
        }
    };
    SDL_GPUVertexAttribute vertexAttributes[] =
    {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(Vertex, pos) },
        {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(Vertex, normal) },
        {.location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(Vertex, uvs) }
    };

    SDL_GPUColorTargetDescription colorTargetDesc
    {
        .format = colorTargetFormat,
        .blend_state = {}
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo
    {
        .vertex_shader = mtl.VertexShader,
        .fragment_shader = mtl.FragmentShader,
        .vertex_input_state =
        {
            .vertex_buffer_descriptions = vertexBufDescriptions,
            .num_vertex_buffers = 1,
            .vertex_attributes = vertexAttributes,
            .num_vertex_attributes = std::size(vertexAttributes)
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state =
        {
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = SDL_GPU_CULLMODE_BACK,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .enable_depth_clip = true
        },
        .depth_stencil_state =
        {
            .compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
            .enable_depth_test = true,
            .enable_depth_write = true
        },
        .target_info =
        {
            .color_target_descriptions = &colorTargetDesc,
            .num_color_targets = 1,
            .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            .has_depth_stencil_target = true
        }
    };

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(m_GpuDevice, &pipelineCreateInfo);
    expect(pipeline, SDL_GetError());

    m_PipelinesByKey.emplace(key, pipeline);

    return pipeline;
}

//private:

std::expected<VertexBuffer*, Error>
SDLGPUDevice::CreateVertexBuffer(const std::span<Vertex>& vertices)
{
    auto result =
        CreateGpuBuffer(m_GpuDevice, SDL_GPU_BUFFERUSAGE_VERTEX, vertices.data(), vertices.size() * sizeof(vertices[0]));
    expect(result, result.error());

    return new SDLVertexBuffer(m_GpuDevice, result.value());
}

std::expected<IndexBuffer*, Error>
SDLGPUDevice::CreateIndexBuffer(const std::span<VertexIndex>& indices)
{
    auto result =
        CreateGpuBuffer(m_GpuDevice, SDL_GPU_BUFFERUSAGE_INDEX, indices.data(), indices.size() * sizeof(indices[0]));
    expect(result, result.error());

    return new SDLIndexBuffer(m_GpuDevice, result.value());
}

std::expected<SDL_GPUTexture*, Error>
SDLGPUDevice::GetOrCreateTextureFromPNG(const std::string_view path)
{
    SDL_GPUTexture* texture = GetTexture(path);

    if (!texture)
    {
        auto imgResult = Image::LoadPng(path);
        expect(imgResult, imgResult.error());
        auto img = *imgResult;
        auto texResult = CreateTexture(m_GpuDevice, img->Width, img->Height, img->Pixels);
        expect(texResult, texResult.error());

        texture = texResult.value();

        m_TexturesByName.emplace(NAME_HASHER(path), TextureRecord{ .Name{path}, .Texture = texture });
    }

    return texture;
}

SDL_GPUTexture*
SDLGPUDevice::GetTexture(const std::string_view path)
{
    auto range = m_TexturesByName.equal_range(NAME_HASHER(path));

    for (auto it = range.first; it != range.second; ++it)
    {
        if (path == it->second.Name)
        {
            return it->second.Texture;
        }
    }

    return nullptr;
}

SDL_GPUShader*
SDLGPUDevice::GetVertexShader(const std::string_view path)
{
    auto range = m_VertexShadersByName.equal_range(NAME_HASHER(path));

    for (auto it = range.first; it != range.second; ++it)
    {
        if (path == it->second.Name)
        {
            return it->second.Shader;
        }
    }

    return nullptr;
}

SDL_GPUShader*
SDLGPUDevice::GetFragShader(const std::string_view path)
{
    auto range = m_FragShadersByName.equal_range(NAME_HASHER(path));

    for (auto it = range.first; it != range.second; ++it)
    {
        if (path == it->second.Name)
        {
            return it->second.Shader;
        }
    }

    return nullptr;
}

static std::expected<SDL_GPUBuffer*, Error> CreateGpuBuffer(
    SDL_GPUDevice* gpuDevice,
    const SDL_GPUBufferUsageFlags usageFlags,
    const void* bufferData,
    const unsigned sizeofBuffer)
{
    SDL_GPUBufferCreateInfo bufferCreateInfo
    {
        .usage = usageFlags,
        .size = sizeofBuffer
    };
    SDL_GPUBuffer* gpuBuffer = SDL_CreateGPUBuffer(gpuDevice, &bufferCreateInfo);
    expect(gpuBuffer, SDL_GetError());

    auto gpuBufAd = AutoDeleter(SDL_ReleaseGPUBuffer, gpuDevice, gpuBuffer);

    if (bufferData)
    {
        auto result = CopyToGpuBuffer(gpuDevice, gpuBuffer, bufferData, sizeofBuffer);
        expect(result, result.error());
    }

    gpuBufAd.Cancel();
    return gpuBuffer;
}

static std::expected<void, Error> CopyToGpuBuffer(
    SDL_GPUDevice* gpuDevice,
    SDL_GPUBuffer* gpuBuffer,
    const void* data,
    const unsigned sizeofData)
{
    //Create a transfer buffer
    SDL_GPUTransferBufferCreateInfo xferBufCreateInfo
    {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeofData
    };
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(gpuDevice, &xferBufCreateInfo);
    expect(transferBuffer, SDL_GetError());
    auto tbufferAd = AutoDeleter(SDL_ReleaseGPUTransferBuffer, gpuDevice, transferBuffer);

    //Copy to transfer buffer
    void* xferBuf = SDL_MapGPUTransferBuffer(gpuDevice, transferBuffer, false);
    expect(xferBuf, SDL_GetError());

    ::memcpy(xferBuf, data, sizeofData);

    SDL_UnmapGPUTransferBuffer(gpuDevice, transferBuffer);

    //Upload data to the buffer.

    SDL_GPUCommandBuffer* uploadCmdBuf = SDL_AcquireGPUCommandBuffer(gpuDevice);
    expect(uploadCmdBuf, SDL_GetError());
    auto cmdBufAd = AutoDeleter(SDL_CancelGPUCommandBuffer, uploadCmdBuf);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmdBuf);
    expect(copyPass, SDL_GetError());

    SDL_GPUTransferBufferLocation xferBufLoc
    {
        .transfer_buffer = transferBuffer,
        .offset = 0
    };
    SDL_GPUBufferRegion bufferRegion
    {
        .buffer = gpuBuffer,
        .offset = 0,
        .size = sizeofData
    };

    SDL_UploadToGPUBuffer(copyPass, &xferBufLoc, &bufferRegion, false);

    SDL_EndGPUCopyPass(copyPass);

    cmdBufAd.Cancel();

    expect(SDL_SubmitGPUCommandBuffer(uploadCmdBuf), SDL_GetError());

    return {};
}

static std::expected<SDL_GPUTexture*, std::string> CreateTexture(
    SDL_GPUDevice* gpuDevice,
    const unsigned width,
    const unsigned height,
    const uint8_t* pixels)
{
    // Create GPU texture
    SDL_GPUTextureCreateInfo textureInfo =
    {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1
    };

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(gpuDevice, &textureInfo);
    expect(texture, SDL_GetError());
    auto texAd = AutoDeleter(SDL_ReleaseGPUTexture, gpuDevice, texture);

    const unsigned sizeofData = width * height * 4;

    SDL_GPUTransferBufferCreateInfo xferBufferCreateInfo
    {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeofData
    };

    // Create transfer buffer for uploading pixel data
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(gpuDevice, &xferBufferCreateInfo);
    expect(transferBuffer, SDL_GetError());
    auto tbufferAd = AutoDeleter(SDL_ReleaseGPUTransferBuffer, gpuDevice, transferBuffer);

    // Copy pixel data to transfer buffer
    void* mappedData = SDL_MapGPUTransferBuffer(gpuDevice, transferBuffer, false);
    expect(mappedData, SDL_GetError());

    ::memcpy(mappedData, pixels, sizeofData);

    SDL_UnmapGPUTransferBuffer(gpuDevice, transferBuffer);

    // Upload to GPU texture
    SDL_GPUCommandBuffer* cmdBuffer = SDL_AcquireGPUCommandBuffer(gpuDevice);
    expect(cmdBuffer, SDL_GetError());
    auto cmdBufAd = AutoDeleter(SDL_CancelGPUCommandBuffer, cmdBuffer);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuffer);
    expect(copyPass, SDL_GetError());

    SDL_GPUTextureTransferInfo transferInfo
    {
        .transfer_buffer = transferBuffer,
        .offset = 0,
        .pixels_per_row = width,
        .rows_per_layer = height
    };

    SDL_GPUTextureRegion textureRegion
    {
        .texture = texture,
        .w = width,
        .h = height,
        .d = 1
    };

    SDL_UploadToGPUTexture(copyPass, &transferInfo, &textureRegion, false);

    SDL_EndGPUCopyPass(copyPass);

    cmdBufAd.Cancel();
    expect(SDL_SubmitGPUCommandBuffer(cmdBuffer), SDL_GetError());

    texAd.Cancel();

    return texture;
}

std::expected<SDL_GPUShader*, Error> LoadShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    SDL_GPUShaderStage shaderStage,
    const unsigned numUniformBuffers,
    const unsigned numSamplers)
{
    expect(fileName.size() > 0, "Invalid shader file name");

    auto fnWithExt = std::string(fileName) + SHADER_EXTENSION;
    size_t fileSize;
    void* shaderSrc = SDL_LoadFile(fnWithExt.data(), &fileSize);
    expect(shaderSrc, "{}: {}", fnWithExt, SDL_GetError());

    auto ad = AutoDeleter(SDL_free, shaderSrc);

    SDL_GPUShaderCreateInfo shaderCreateInfo
    {
        .code_size = fileSize,
        .code = (uint8_t*)shaderSrc,
        .entrypoint = "main",
        .format = SHADER_FORMAT,
        .stage = shaderStage,
        .num_samplers = numSamplers,
        .num_uniform_buffers = numUniformBuffers
    };

    SDL_GPUShader* shader = SDL_CreateGPUShader(gpuDevice, &shaderCreateInfo);
    expect(shader, SDL_GetError());

    return shader;
}
