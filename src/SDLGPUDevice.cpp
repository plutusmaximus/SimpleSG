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

SDLGpuTexture::~SDLGpuTexture()
{
    if (Texture) { SDL_ReleaseGPUTexture(m_GpuDevice, Texture); }
    if (Sampler) { SDL_ReleaseGPUSampler(m_GpuDevice, Sampler); }
}

SDLGpuVertexShader::~SDLGpuVertexShader()
{
    if (Shader) { SDL_ReleaseGPUShader(m_GpuDevice, Shader); }
}

SDLGpuFragmentShader::~SDLGpuFragmentShader()
{
    if (Shader) { SDL_ReleaseGPUShader(m_GpuDevice, Shader); }
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
    SDL_GPUDevice* sdlDevice = SDL_CreateGPUDevice(SHADER_FORMAT, debugMode, DRIVER_NAME);
    expect(sdlDevice, SDL_GetError());

    auto sdlDeviceCleanup = Finally([sdlDevice]()
    {
        SDL_DestroyGPUDevice(sdlDevice);
    });

    if (!SDL_ClaimWindowForGPUDevice(sdlDevice, window))
    {
        return std::unexpected(SDL_GetError());
    }

    if(!SDL_SetGPUSwapchainParameters(
        sdlDevice,
        window,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        SDL_GPU_PRESENTMODE_MAILBOX))
    {
        return std::unexpected(SDL_GetError());
    }

    SDLGPUDevice* device = new SDLGPUDevice(window, sdlDevice);

    expectv(device, "Error allocating device");

    sdlDeviceCleanup.Cancel();

    return device;
}

SDLGPUDevice::~SDLGPUDevice()
{
    for (const auto& [_, pipeline] : m_PipelinesByKey)
    {
        SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
    }

    if (Device)
    {
        SDL_DestroyGPUDevice(Device);
    }
}

Extent SDLGPUDevice::GetExtent() const
{
    int width = 0, height = 0;
    if(!SDL_GetWindowSizeInPixels(Window, &width, &height))
    {
        logError("Failed to get window size: {}", SDL_GetError());
    }
    return Extent{static_cast<float>(width), static_cast<float>(height)};
}

Result<std::tuple<GpuVertexBuffer, GpuIndexBuffer>>
SDLGPUDevice::CreateBuffers(
    const std::span<std::span<const Vertex>>& vertices,
    const std::span<std::span<const uint32_t>>& indices)
{
    if(!everify(vertices.size() == indices.size()))
    {
        return std::unexpected("Mismatched number of vertex and index buffers");
    }

    const size_t numSrcBuffers = vertices.size();
    size_t sizeofVtxBuffer = 0;
    size_t sizeofIdxBuffer = 0;

    for(int i = 0; i < numSrcBuffers; ++i)
    {
        sizeofVtxBuffer += vertices[i].size() * sizeof(vertices[i][0]);
        sizeofIdxBuffer += indices[i].size() * sizeof(indices[i][0]);
    }

    const size_t sizeofData = sizeofVtxBuffer + sizeofIdxBuffer;

    //Create a single buffer to contain vertices and indices.

    SDL_GPUBufferCreateInfo bufferCreateInfo
    {
        .usage = SDL_GPU_BUFFERUSAGE_VERTEX | SDL_GPU_BUFFERUSAGE_INDEX,
        .size = static_cast<Uint32>(sizeofData)
    };

    SDL_GPUBuffer* buf = SDL_CreateGPUBuffer(Device, &bufferCreateInfo);
    expect(buf, SDL_GetError());

    RefPtr<SDLGpuBuffer> gpuBuf = new SDLGpuBuffer(Device, buf);

    expectv(gpuBuf, "Error allocating SDLGPUBuffer");

    //Transfer vertex/index data to GPU memory.

    //Create transfer buffer
    SDL_GPUTransferBufferCreateInfo xferBufCreateInfo
    {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<Uint32>(sizeofData)
    };

    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(Device, &xferBufCreateInfo);
    expect(transferBuffer, SDL_GetError());
    auto tbufferCleanup = Finally([&]()
    {
        SDL_ReleaseGPUTransferBuffer(Device, transferBuffer);
    });

    // Copy to transfer buffer
    uint8_t* xferBuf = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(Device, transferBuffer, false));
    expect(xferBuf, SDL_GetError());

    const size_t bufferOffsetOfVerts = 0;
    const size_t bufferOffsetOfIndices = sizeofVtxBuffer;

    Vertex* dstVtx = reinterpret_cast<Vertex*>(xferBuf + bufferOffsetOfVerts);
    VertexIndex* dstIdx = reinterpret_cast<VertexIndex*>(xferBuf + bufferOffsetOfIndices);

    const Vertex* vtxBase = dstVtx;

    for(int i = 0; i < numSrcBuffers; ++i)
    {
        // Offset to add to indices to account for multiple vertex buffers.
        const uint32_t vtxOffset = static_cast<uint32_t>(dstVtx - vtxBase);

        const auto& vertSpan = vertices[i];
        if(!vertSpan.empty())
        {
            const uint32_t vtxCount = static_cast<uint32_t>(vertSpan.size());
            const uint32_t vtxBufSize = static_cast<uint32_t>(vtxCount * sizeof(Vertex));
            ::memcpy(dstVtx, vertSpan.data(), vtxBufSize);
            dstVtx += vtxCount;
        }

        const auto& indexSpan = indices[i];
        if(!indexSpan.empty())
        {
            const uint32_t idxCount = static_cast<uint32_t>(indexSpan.size());
            for(int j = 0; j < idxCount; ++j, ++dstIdx)
            {
                *dstIdx = indexSpan[j] + vtxOffset;
            }
        }
    }

    SDL_UnmapGPUTransferBuffer(Device, transferBuffer);

    //Upload data to GPU mem.
    SDL_GPUCommandBuffer* uploadCmdBuf = SDL_AcquireGPUCommandBuffer(Device);
    expect(uploadCmdBuf, SDL_GetError());
    auto cmdBufCleanup = Finally([&]()
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
        .size = static_cast<Uint32>(sizeofData)
    };

    SDL_UploadToGPUBuffer(copyPass, &srcBuf, &dstBuf, false);

    SDL_EndGPUCopyPass(copyPass);

    expect(SDL_SubmitGPUCommandBuffer(uploadCmdBuf), SDL_GetError());

    cmdBufCleanup.Cancel();

    GpuVertexBuffer vb{ gpuBuf, static_cast<uint32_t>(bufferOffsetOfVerts) };
    GpuIndexBuffer ib{ gpuBuf, static_cast<uint32_t>(bufferOffsetOfIndices) };

    return std::make_tuple(vb, ib);
}

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

Result<RefPtr<GpuTexture>>
SDLGPUDevice::CreateTexture(const TextureSpec& textureSpec)
{
    auto acceptor = overloaded
    {
        [this](const std::string& path) { return CreateTexture(path); },
        [this](const RefPtr<Image>& image) { return CreateTexture(image); },
        [this](const RgbaColorf& color) { return CreateTexture(color); }
    };

    return std::visit(acceptor, textureSpec.Source);
}

Result<RefPtr<GpuVertexShader>>
SDLGPUDevice::CreateVertexShader(const VertexShaderSpec& shaderSpec)
{
    const std::string_view path = std::get<std::string>(shaderSpec.Source);
    auto shaderResult = LoadShader(
        Device,
        path,
        SDL_GPU_SHADERSTAGE_VERTEX,
        shaderSpec.NumUniformBuffers,
        0);

    expect(shaderResult, shaderResult.error());

    RefPtr<GpuVertexShader> gpuShader = new SDLGpuVertexShader(Device, shaderResult.value());

    if(!gpuShader)
    {
        return std::unexpected("Error allocating SDLGPUVertexShader");
    }

    return gpuShader;
}

Result<RefPtr<GpuFragmentShader>>
SDLGPUDevice::CreateFragmentShader(const FragmentShaderSpec& shaderSpec)
{
    const std::string_view path = std::get<std::string>(shaderSpec.Source);
    auto shaderResult = LoadShader(
        Device,
        path,
        SDL_GPU_SHADERSTAGE_FRAGMENT,
        0,
        shaderSpec.NumSamplers);

    expect(shaderResult, shaderResult.error());

    RefPtr<GpuFragmentShader> gpuShader = new SDLGpuFragmentShader(Device, shaderResult.value());

    if(!gpuShader)
    {
        return std::unexpected("Error allocating SDLGPUFragmentShader");
    }

    return gpuShader;
}

Result<SDL_GPUGraphicsPipeline*>
SDLGPUDevice::GetOrCreatePipeline(const Material& mtl)
{
    SDL_GPUTextureFormat colorTargetFormat = SDL_GetGPUSwapchainTextureFormat(Device, Window);
    PipelineKey key
    {
        .ColorFormat = colorTargetFormat,
        .VertexShader = mtl.VertexShader.Get<SDLGpuVertexShader>()->Shader,
        .FragShader = mtl.FragmentShader.Get<SDLGpuFragmentShader>()->Shader
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
        {.location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(Vertex, uvs[0]) }
    };

    SDL_GPUColorTargetDescription colorTargetDesc
    {
        .format = colorTargetFormat,
        .blend_state =
        {
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .color_write_mask = SDL_GPU_COLORCOMPONENT_R |
                               SDL_GPU_COLORCOMPONENT_G |
                               SDL_GPU_COLORCOMPONENT_B |
                               SDL_GPU_COLORCOMPONENT_A,
            .enable_blend = true,
            .enable_color_write_mask = false,
        }
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo
    {
        .vertex_shader = mtl.VertexShader.Get<SDLGpuVertexShader>()->Shader,
        .fragment_shader = mtl.FragmentShader.Get<SDLGpuFragmentShader>()->Shader,
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

Result<RefPtr<GpuTexture>>
SDLGPUDevice::CreateTexture(const RefPtr<Image> image)
{
    return CreateTexture(image->Width, image->Height, image->Pixels);
}

Result<RefPtr<GpuTexture>>
SDLGPUDevice::CreateTexture(const RgbaColorf& color)
{
    const uint8_t pixelData[4]
    {
        static_cast<uint8_t>(std::clamp(color.r * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(color.g * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(color.b * 255.0f, 0.0f, 255.0f)),
        static_cast<uint8_t>(std::clamp(color.a * 255.0f, 0.0f, 255.0f))
    };

    return CreateTexture(1, 1, pixelData);
}

Result<RefPtr<GpuTexture>>
SDLGPUDevice::CreateTexture(const unsigned width, const unsigned height, const uint8_t* pixels)
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

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(Device, &textureInfo);
    expect(texture, SDL_GetError());
    auto texCleanup = Finally([&]()
    {
        SDL_ReleaseGPUTexture(Device, texture);
    });

    const unsigned sizeofData = textureInfo.width * textureInfo.height * 4;

    SDL_GPUTransferBufferCreateInfo xferBufferCreateInfo
    {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeofData
    };

    // Create transfer buffer for uploading pixel data
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(Device, &xferBufferCreateInfo);
    expect(transferBuffer, SDL_GetError());
    auto tbufferCleanup = Finally([&]()
    {
        SDL_ReleaseGPUTransferBuffer(Device, transferBuffer);
    });

    // Copy pixel data to transfer buffer
    void* mappedData = SDL_MapGPUTransferBuffer(Device, transferBuffer, false);
    expect(mappedData, SDL_GetError());

    ::memcpy(mappedData, pixels, sizeofData);

    SDL_UnmapGPUTransferBuffer(Device, transferBuffer);

    // Upload to GPU texture
    SDL_GPUCommandBuffer* cmdBuffer = SDL_AcquireGPUCommandBuffer(Device);
    expect(cmdBuffer, SDL_GetError());
    auto cmdBufCleanup = Finally([&]()
    {
        SDL_CancelGPUCommandBuffer(cmdBuffer);
    });

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuffer);
    expect(copyPass, SDL_GetError());

    SDL_GPUTextureTransferInfo transferInfo
    {
        .transfer_buffer = transferBuffer,
        .offset = 0,
        .pixels_per_row = textureInfo.width,
        .rows_per_layer = textureInfo.height
    };

    SDL_GPUTextureRegion textureRegion
    {
        .texture = texture,
        .w = textureInfo.width,
        .h = textureInfo.height,
        .d = 1
    };

    SDL_UploadToGPUTexture(copyPass, &transferInfo, &textureRegion, false);

    SDL_EndGPUCopyPass(copyPass);

    cmdBufCleanup.Cancel();
    expect(SDL_SubmitGPUCommandBuffer(cmdBuffer), SDL_GetError());

    // Create sampler
    SDL_GPUSamplerCreateInfo samplerInfo =
    {
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT
    };

    SDL_GPUSampler* sampler = SDL_CreateGPUSampler(Device, &samplerInfo);
    expect(sampler, SDL_GetError());

    auto samplerCleanup = Finally([&]()
    {
        SDL_ReleaseGPUSampler(Device, sampler);
    });

    RefPtr<SDLGpuTexture> gpuTex = new SDLGpuTexture(Device, texture, sampler);

    expectv(gpuTex, "Error allocating SDLGPUTexture");

    texCleanup.Cancel();
    samplerCleanup.Cancel();

    return gpuTex;
}

Result<RefPtr<GpuTexture>>
SDLGPUDevice::CreateTexture(const std::string_view path)
{
    auto imgResult = Image::LoadFromFile(path);
    expect(imgResult, imgResult.error());
    auto img = *imgResult;
    return CreateTexture(img);
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
