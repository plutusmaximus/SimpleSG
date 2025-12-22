#include "SDLGPUDevice.h"

#include "Finally.h"
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

static Result<SDL_GPUTexture*> CreateTexture(
    SDL_GPUDevice* gpuDevice,
    const unsigned width,
    const unsigned height,
    const uint8_t* pixels);

static Result<SDL_GPUShader*> LoadShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    SDL_GPUShaderStage shaderStage,
    const unsigned numUniformBuffers,
    const unsigned numSamplers);

SDLGpuBuffer::~SDLGpuBuffer()
{
    if (Buffer) { SDL_ReleaseGPUBuffer(m_GpuDevice, Buffer); }
}

SDLGPUDevice::SDLGPUDevice(SDL_Window* window, SDL_GPUDevice* gpuDevice)
    : Window(window)
    , Device(gpuDevice)
{
}

Result<RefPtr<SDLGPUDevice>>
SDLGPUDevice::Create(SDL_Window* window)
{
    logInfo("Creating SDL GPU Device...");

    //TODO - move these to environment variables.
    expect(SDL_SetHint(SDL_HINT_RENDER_VULKAN_DEBUG, "1"), SDL_GetError());
    expect(SDL_SetHint(SDL_HINT_RENDER_GPU_DEBUG, "1"), SDL_GetError());

    // Initialize GPU device
    const bool debugMode = true;
    SDL_GPUDevice* gpuDevice = SDL_CreateGPUDevice(SHADER_FORMAT, debugMode, DRIVER_NAME);
    expect(gpuDevice, SDL_GetError());

    if (!SDL_ClaimWindowForGPUDevice(gpuDevice, window))
    {
        SDL_DestroyGPUDevice(gpuDevice);
        return std::unexpected(SDL_GetError());
    }

    if(!SDL_SetGPUSwapchainParameters(
        gpuDevice,
        window,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        SDL_GPU_PRESENTMODE_MAILBOX))
    {
        SDL_DestroyGPUDevice(gpuDevice);
        return std::unexpected(SDL_GetError());
    }

    SDLGPUDevice* device = new SDLGPUDevice(window, gpuDevice);

    expectv(device, "Error allocating device");

    return device;
}

SDLGPUDevice::~SDLGPUDevice()
{
    for (auto mtl : m_Materials)
    {
        delete mtl;
    }

    for (const auto& [_, pipeline] : m_PipelinesByKey)
    {
        SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
    }

    if (m_Sampler)
    {
        SDL_ReleaseGPUSampler(Device, m_Sampler);
    }

    for (const auto& [_, rec] : m_TexturesByName)
    {
        SDL_ReleaseGPUTexture(Device, rec.Item);
    }

    for (const auto& [_, rec] : m_VertexShadersByName)
    {
        SDL_ReleaseGPUShader(Device, rec.Item);
    }

    for (const auto& [_, rec] : m_FragShadersByName)
    {
        SDL_ReleaseGPUShader(Device, rec.Item);
    }

    if (Device)
    {
        SDL_DestroyGPUDevice(Device);
    }
}

Result<RefPtr<Model>>
SDLGPUDevice::CreateModel(const ModelSpec& modelSpec)
{
    auto bufResult = CreateBuffers(modelSpec.Vertices, modelSpec.Indices);
    expect(bufResult, bufResult.error());

    auto [vb, ib] = bufResult.value();

    std::vector<Mesh> meshes;

    for (const auto& meshSpec : modelSpec.MeshSpecs)
    {
        SDL_GPUTexture* albedo = nullptr;

        if (!meshSpec.MtlSpec.Albedo.empty())
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

                m_Sampler = SDL_CreateGPUSampler(Device, &samplerInfo);
                expect(m_Sampler, SDL_GetError());
            }

            //TODO - check material DB for existing material

            auto albedoResult = GetOrCreateTexture(meshSpec.MtlSpec.Albedo);
            expect(albedoResult, albedoResult.error());

            albedo = albedoResult.value();
        }

        //FIXME - specify number of uniform buffers.
        auto vertexShaderResult = GetOrCreateVertexShader(meshSpec.MtlSpec.VertexShader, 3);
        expect(vertexShaderResult, vertexShaderResult.error());

        //FIXME - specify number of uniform samplers.
        auto fragShaderResult = GetOrCreateFragmentShader(meshSpec.MtlSpec.FragmentShader, 1);
        expect(fragShaderResult, fragShaderResult.error());

        SDLMaterial* mtl = new SDLMaterial(
            meshSpec.MtlSpec.Color,
            albedo,
            m_Sampler,
            vertexShaderResult.value(),
            fragShaderResult.value());

        expectv(mtl, "Error allocating SDLMaterial");

        m_MaterialIndexById.emplace(mtl->Id, std::size(m_Materials));
        m_Materials.emplace_back(mtl);

        Mesh mesh(vb, ib, meshSpec.IndexOffset, meshSpec.IndexCount, mtl->Id);

        meshes.emplace_back(mesh);
    }

    return Model::Create(meshes);
}

Result<const SDLMaterial*>
SDLGPUDevice::GetMaterial(const MaterialId& mtlId) const
{
    auto it = m_MaterialIndexById.find(mtlId);
    if (m_MaterialIndexById.end() == it)
    {
        return std::unexpected("Invalid material ID");
    }

    return m_Materials[it->second];
}

Result<SDL_GPUShader*>
SDLGPUDevice::GetOrCreateVertexShader(
    const std::string_view path,
    const int numUniformBuffers)
{
    SDL_GPUShader* shader = GetVertexShader(path);

    if (!shader)
    {
        auto shaderResult = LoadShader(Device, path, SDL_GPU_SHADERSTAGE_VERTEX, numUniformBuffers, 0);

        expect(shaderResult, shaderResult.error());

        shader = shaderResult.value();

        const HashKey hashKey = MakeHashKey(path);

        m_VertexShadersByName.Add(path, shader);
    }

    return shader;
}

Result<SDL_GPUShader*>
SDLGPUDevice::GetOrCreateFragmentShader(
    const std::string_view path,
    const int numSamplers)
{
    SDL_GPUShader* shader = GetFragShader(path);

    if (!shader)
    {
        auto shaderResult = LoadShader(Device, path, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, numSamplers);

        expect(shaderResult, shaderResult.error());

        shader = shaderResult.value();

        const HashKey hashKey = MakeHashKey(path);

        m_FragShadersByName.Add(path, shader);
    }

    return shader;
}

Result<SDL_GPUGraphicsPipeline*>
SDLGPUDevice::GetOrCreatePipeline(const SDLMaterial& mtl)
{
    SDL_GPUTextureFormat colorTargetFormat = SDL_GetGPUSwapchainTextureFormat(Device, Window);
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
            .front_face = SDL_GPU_FRONTFACE_CLOCKWISE,
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

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(Device, &pipelineCreateInfo);
    expect(pipeline, SDL_GetError());

    m_PipelinesByKey.emplace(key, pipeline);

    return pipeline;
}

//private:

Result<std::tuple<VertexBuffer, IndexBuffer>>
SDLGPUDevice::CreateBuffers(
    const std::span<const Vertex>& vertices,
    const std::span<const VertexIndex>& indices)
{
    const uint32_t sizeofVerts = static_cast<Uint32>(vertices.size() * sizeof(vertices[0]));
    const uint32_t sizeofIndices = static_cast<Uint32>(indices.size() * sizeof(indices[0]));
    const uint32_t sizeofData = sizeofVerts + sizeofIndices;

    //Create a single buffer to contain vertices and indices.

    SDL_GPUBufferCreateInfo bufferCreateInfo{};

    bufferCreateInfo.usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_INDEX;
    bufferCreateInfo.size = sizeofData;

    SDL_GPUBuffer* buf = SDL_CreateGPUBuffer(Device, &bufferCreateInfo);
    expect(buf, SDL_GetError());

    RefPtr<SDLGpuBuffer> gpuBuf = new SDLGpuBuffer(Device, buf);

    expectv(gpuBuf, "Error allocating SDLGPUBuffer");

    //Transfer vertex/index data to GPU memory.

    //Create transfer buffer
    SDL_GPUTransferBufferCreateInfo xferBufCreateInfo
    {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeofData
    };

    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(Device, &xferBufCreateInfo);
    expect(transferBuffer, SDL_GetError());
    auto tbufferFin = Finally([&]()
    {
        SDL_ReleaseGPUTransferBuffer(Device, transferBuffer);
    });

    //Copy to transfer buffer
    void* xferBuf = SDL_MapGPUTransferBuffer(Device, transferBuffer, false);
    expect(xferBuf, SDL_GetError());

    ::memcpy(xferBuf, vertices.data(), sizeofVerts);
    ::memcpy(&((char*)xferBuf)[sizeofVerts], indices.data(), sizeofIndices);

    SDL_UnmapGPUTransferBuffer(Device, transferBuffer);

    //Upload data to GPU mem.
    SDL_GPUCommandBuffer* uploadCmdBuf = SDL_AcquireGPUCommandBuffer(Device);
    expect(uploadCmdBuf, SDL_GetError());
    auto cmdBufFin = Finally([&]()
    {
        SDL_CancelGPUCommandBuffer(uploadCmdBuf);
    });

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmdBuf);
    expect(copyPass, SDL_GetError());

    SDL_GPUTransferBufferLocation srcBuf
    {
        .transfer_buffer = transferBuffer,
        .offset = 0
    };
    SDL_GPUBufferRegion dstBuf
    {
        .buffer = gpuBuf->Buffer,
        .offset = 0,
        .size = sizeofData
    };

    SDL_UploadToGPUBuffer(copyPass, &srcBuf, &dstBuf, false);

    SDL_EndGPUCopyPass(copyPass);

    expect(SDL_SubmitGPUCommandBuffer(uploadCmdBuf), SDL_GetError());

    cmdBufFin.Cancel();

    VertexBuffer vb{ gpuBuf, 0 };
    IndexBuffer ib{ gpuBuf, sizeofVerts };

    return std::make_tuple(vb, ib);
}

Result<SDL_GPUTexture*>
SDLGPUDevice::GetOrCreateTexture(const std::string_view path)
{
    SDL_GPUTexture* texture = GetTexture(path);

    if (!texture)
    {
        auto imgResult = Image::Load(path);
        expect(imgResult, imgResult.error());
        auto img = *imgResult;
        auto texResult = CreateTexture(Device, img->Width, img->Height, img->Pixels);
        expect(texResult, texResult.error());

        texture = texResult.value();

        const HashKey hashKey = MakeHashKey(path);

        m_TexturesByName.Add(path, texture);
    }

    return texture;
}

SDL_GPUTexture*
SDLGPUDevice::GetTexture(const std::string_view path)
{
    return m_TexturesByName.Find(path);
}

SDL_GPUShader*
SDLGPUDevice::GetVertexShader(const std::string_view path)
{
    return m_VertexShadersByName.Find(path);
}

SDL_GPUShader*
SDLGPUDevice::GetFragShader(const std::string_view path)
{
    return m_FragShadersByName.Find(path);
}

static Result<SDL_GPUTexture*> CreateTexture(
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
    auto texFin = Finally([&]()
    {
        SDL_ReleaseGPUTexture(gpuDevice, texture);
    });

    const unsigned sizeofData = width * height * 4;

    SDL_GPUTransferBufferCreateInfo xferBufferCreateInfo
    {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeofData
    };

    // Create transfer buffer for uploading pixel data
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(gpuDevice, &xferBufferCreateInfo);
    expect(transferBuffer, SDL_GetError());
    auto tbufferFin = Finally([&]()
    {
        SDL_ReleaseGPUTransferBuffer(gpuDevice, transferBuffer);
    });

    // Copy pixel data to transfer buffer
    void* mappedData = SDL_MapGPUTransferBuffer(gpuDevice, transferBuffer, false);
    expect(mappedData, SDL_GetError());

    ::memcpy(mappedData, pixels, sizeofData);

    SDL_UnmapGPUTransferBuffer(gpuDevice, transferBuffer);

    // Upload to GPU texture
    SDL_GPUCommandBuffer* cmdBuffer = SDL_AcquireGPUCommandBuffer(gpuDevice);
    expect(cmdBuffer, SDL_GetError());
    auto cmdBufFin = Finally([&]()
    {
        SDL_CancelGPUCommandBuffer(cmdBuffer);
    });

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

    cmdBufFin.Cancel();
    expect(SDL_SubmitGPUCommandBuffer(cmdBuffer), SDL_GetError());

    texFin.Cancel();

    return texture;
}

Result<SDL_GPUShader*> LoadShader(
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

    auto cleanup = Finally([&]()
    {
        SDL_free(shaderSrc);
    });

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
