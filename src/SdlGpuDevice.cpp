#define __LOGGER_NAME__ "SDL "

#include "SdlGpuDevice.h"

#include "Logging.h"

#include "SdlRenderGraph.h"
#include "scope_exit.h"

#include "Stopwatch.h"

#include <SDL3/SDL.h>
#include <algorithm>

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

/// @brief Common function to load a shader from a file.
template<typename T>
static Result<SDL_GPUShader*> LoadShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    const unsigned numUniformBuffers,
    const unsigned numSamplers);

/// @brief Traits to map a CPU-side buffer type to its corresponding GPU buffer type and usage flags.
template<typename T> struct GpuBufferTraits;

template<> struct GpuBufferTraits<VertexIndex>
{
    static constexpr SDL_GPUBufferUsageFlags Usage = SDL_GPU_BUFFERUSAGE_INDEX;
};

template<> struct GpuBufferTraits<Vertex>
{
    static constexpr SDL_GPUBufferUsageFlags Usage = SDL_GPU_BUFFERUSAGE_VERTEX;
};

/// @brief Common function to create a GPU buffer from multiple spans.
template<typename T>
static Result<std::tuple<SDL_GPUBuffer*, size_t>>
CreateGpuBuffer(SDL_GPUDevice* gd, const std::span<const std::span<const T>>& spans);

SdlGpuVertexBuffer::~SdlGpuVertexBuffer()
{
    SDL_ReleaseGPUBuffer(m_GpuDevice, m_Buffer);
}

SdlGpuIndexBuffer::~SdlGpuIndexBuffer()
{
    SDL_ReleaseGPUBuffer(m_GpuDevice, m_Buffer);
}

SdlGpuTexture::~SdlGpuTexture()
{
    if (m_Texture) { SDL_ReleaseGPUTexture(m_GpuDevice, m_Texture); }
    // Sampler is released by the SDLGPUDevice destructor.
}

SdlGpuVertexShader::~SdlGpuVertexShader()
{
    if (m_Shader) { SDL_ReleaseGPUShader(m_GpuDevice, m_Shader); }
}

SdlGpuFragmentShader::~SdlGpuFragmentShader()
{
    if (m_Shader) { SDL_ReleaseGPUShader(m_GpuDevice, m_Shader); }
}

SdlGpuDevice::SdlGpuDevice(SDL_Window* window, SDL_GPUDevice* gpuDevice)
    : Window(window)
    , Device(gpuDevice)
{
}

Result<GpuDevice*>
SdlGpuDevice::Create(SDL_Window* window)
{
    logInfo("Creating SDL GPU Device...");

    //TODO - move these to environment variables.
    expect(SDL_SetHint(SDL_HINT_RENDER_VULKAN_DEBUG, "1"), SDL_GetError());
    expect(SDL_SetHint(SDL_HINT_RENDER_GPU_DEBUG, "1"), SDL_GetError());

    // Initialize GPU device
    const bool debugMode = true;
    SDL_GPUDevice* sdlDevice = SDL_CreateGPUDevice(SHADER_FORMAT, debugMode, DRIVER_NAME);
    expect(sdlDevice, SDL_GetError());

    auto sdlDeviceCleanup = scope_exit([sdlDevice]()
    {
        SDL_DestroyGPUDevice(sdlDevice);
    });

    if (!SDL_ClaimWindowForGPUDevice(sdlDevice, window))
    {
        return Error(SDL_GetError());
    }

    if(!SDL_SetGPUSwapchainParameters(
        sdlDevice,
        window,
        SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
        SDL_GPU_PRESENTMODE_MAILBOX))
    {
        return Error(SDL_GetError());
    }

    SdlGpuDevice* device = new SdlGpuDevice(window, sdlDevice);

    expectv(device, "Error allocating device");

    sdlDeviceCleanup.release();

    return device;
}

void SdlGpuDevice::Destroy(GpuDevice* device)
{
    delete device;
}

SdlGpuDevice::~SdlGpuDevice()
{
    for (const auto& [_, pipeline] : m_PipelinesByKey)
    {
        SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
    }

    if(m_Sampler)
    {
        SDL_ReleaseGPUSampler(Device, m_Sampler);
        m_Sampler = nullptr;
    }

    if (Device)
    {
        SDL_DestroyGPUDevice(Device);
    }
}

Extent
SdlGpuDevice::GetExtent() const
{
    int width = 0, height = 0;
    if(!SDL_GetWindowSizeInPixels(Window, &width, &height))
    {
        logError("Failed to get window size: {}", SDL_GetError());
    }
    return Extent{static_cast<float>(width), static_cast<float>(height)};
}

Result<VertexBuffer>
SdlGpuDevice::CreateVertexBuffer(const std::span<const Vertex>& vertices)
{
    std::span<const Vertex> spans[]{vertices};
    return CreateVertexBuffer(spans);
}

Result<VertexBuffer>
SdlGpuDevice::CreateVertexBuffer(const std::span<std::span<const Vertex>>& vertices)
{
    auto nativeBufResult = CreateGpuBuffer<Vertex>(Device, vertices);
    expect(nativeBufResult, nativeBufResult.error());

    auto [nativeBuf, sizeofBuffer] = nativeBufResult.value();

    auto vb = new SdlGpuVertexBuffer(Device, nativeBuf);
    if(!vb)
    {
        SDL_ReleaseGPUBuffer(Device, nativeBuf);
        return Error("Error allocating SDLGpuVertexBuffer");
    }

    const uint32_t count = static_cast<uint32_t>(sizeofBuffer / sizeof(Vertex));

    return VertexBuffer(vb, 0, count);
}

Result<void>
SdlGpuDevice::DestroyVertexBuffer(VertexBuffer& buffer)
{
    auto sdlBuffer = buffer.Get<SdlGpuVertexBuffer>();
    if (!sdlBuffer)
    {
        return Error("Invalid vertex buffer");
    }

    delete sdlBuffer;
    buffer = VertexBuffer(nullptr, 0, 0);

    return ResultOk;
}

Result<IndexBuffer>
SdlGpuDevice::CreateIndexBuffer(const std::span<const VertexIndex>& indices)
{
    std::span<const VertexIndex> spans[]{indices};
    return CreateIndexBuffer(spans);
}

Result<IndexBuffer>
SdlGpuDevice::CreateIndexBuffer(const std::span<std::span<const VertexIndex>>& indices)
{
    auto nativeBufResult = CreateGpuBuffer<VertexIndex>(Device, indices);
    expect(nativeBufResult, nativeBufResult.error());

    auto [nativeBuf, sizeofBuffer] = nativeBufResult.value();

    auto ib = new SdlGpuIndexBuffer(Device, nativeBuf);

    if(!ib)
    {
        SDL_ReleaseGPUBuffer(Device, nativeBuf);
        return Error("Error allocating SDLGpuIndexBuffer");
    }

    const uint32_t count = static_cast<uint32_t>(sizeofBuffer / sizeof(VertexIndex));

    return IndexBuffer(ib, 0, count);
}

Result<void>
SdlGpuDevice::DestroyIndexBuffer(IndexBuffer& buffer)
{
    auto sdlBuffer = buffer.Get<SdlGpuIndexBuffer>();
    if (!sdlBuffer)
    {
        return Error("Invalid index buffer");
    }

    delete sdlBuffer;
    buffer = IndexBuffer(nullptr, 0, 0);

    return ResultOk;
}

Result<Texture>
SdlGpuDevice::CreateTexture(const unsigned width, const unsigned height, const uint8_t* pixels, const unsigned rowStride, const imstring& name)
{
    Stopwatch sw1;

    SDL_PropertiesID props = SDL_CreateProperties();
    auto propsCleanup = scope_exit([&]()
    {
        SDL_DestroyProperties(props);
    });

    SDL_SetStringProperty(props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, name.c_str());

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
        .sample_count = SDL_GPU_SAMPLECOUNT_1,
        .props = props
    };

    Stopwatch sw;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(Device, &textureInfo);
    expect(texture, SDL_GetError());
    auto texCleanup = scope_exit([&]()
    {
        SDL_ReleaseGPUTexture(Device, texture);
    });

    logDebug("SDL_CreateGPUTexture: {} ms", sw.Elapsed() * 1000.0f);
    sw.Mark();

    const unsigned sizeofData = textureInfo.width * textureInfo.height * 4;

    SDL_GPUTransferBufferCreateInfo xferBufferCreateInfo
    {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeofData
    };

    // Create transfer buffer for uploading pixel data
    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(Device, &xferBufferCreateInfo);
    expect(transferBuffer, SDL_GetError());
    auto tbufferCleanup = scope_exit([&]()
    {
        SDL_ReleaseGPUTransferBuffer(Device, transferBuffer);
    });

    logDebug("SDL_CreateGPUTransferBuffer: {} ms", sw.Elapsed() * 1000.0f);
    sw.Mark();

    // Copy pixel data to transfer buffer
    void* mappedData = SDL_MapGPUTransferBuffer(Device, transferBuffer, false);
    expect(mappedData, SDL_GetError());

    logDebug("SDL_MapGPUTransferBuffer: {} ms", sw.Elapsed() * 1000.0f);
    sw.Mark();

    const uint8_t* srcPixels = pixels;
    uint8_t* dstPixels = static_cast<uint8_t*>(mappedData);
    const ptrdiff_t dstRowBytes = textureInfo.width * 4;
    for(unsigned row = 0; row < textureInfo.height; ++row, srcPixels += rowStride, dstPixels += dstRowBytes)
    {
        std::memcpy(dstPixels, srcPixels, textureInfo.width * 4);
    }

    logDebug("memcpy: {} ms", sw.Elapsed() * 1000.0f);
    sw.Mark();

    SDL_UnmapGPUTransferBuffer(Device, transferBuffer);

    logDebug("SDL_UnmapGPUTransferBuffer: {} ms", sw.Elapsed() * 1000.0f);
    sw.Mark();

    // Upload to GPU texture
    SDL_GPUCommandBuffer* cmdBuffer = SDL_AcquireGPUCommandBuffer(Device);
    expect(cmdBuffer, SDL_GetError());
    auto cmdBufCleanup = scope_exit([&]()
    {
        SDL_CancelGPUCommandBuffer(cmdBuffer);
    });

    logDebug("SDL_AcquireGPUCommandBuffer: {} ms", sw.Elapsed() * 1000.0f);
    sw.Mark();

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuffer);
    expect(copyPass, SDL_GetError());

    logDebug("SDL_BeginGPUCopyPass: {} ms", sw.Elapsed() * 1000.0f);
    sw.Mark();

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

    logDebug("SDL_UploadToGPUTexture: {} ms", sw.Elapsed() * 1000.0f);
    sw.Mark();

    SDL_EndGPUCopyPass(copyPass);

    logDebug("SDL_EndGPUCopyPass: {} ms", sw.Elapsed() * 1000.0f);
    sw.Mark();

    cmdBufCleanup.release();
    expect(SDL_SubmitGPUCommandBuffer(cmdBuffer), SDL_GetError());

    logDebug("SDL_SubmitGPUCommandBuffer: {} ms", sw.Elapsed() * 1000.0f);
    sw.Mark();

    if(!m_Sampler)
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

    SdlGpuTexture* gpuTex = new SdlGpuTexture(Device, texture, m_Sampler);

    expectv(gpuTex, "Error allocating SDLGPUTexture");

    texCleanup.release();

    logDebug("SdlGpuDevice::CreateTexture: {} ms", sw1.Elapsed() * 1000.0f);

    return Texture(gpuTex);
}

Result<Texture>
SdlGpuDevice::CreateTexture(const RgbaColorf& color, const imstring& name)
{
    RgbaColoru8 colorU8{color};
    const uint8_t pixelData[4]{colorU8.r, colorU8.g, colorU8.b, colorU8.a};


    return CreateTexture(1, 1, pixelData, 4, name);
}

Result<void>
SdlGpuDevice::DestroyTexture(Texture& texture)
{
    auto sdlTexture = texture.Get<SdlGpuTexture>();
    if (!sdlTexture)
    {
        return Error("Invalid texture");
    }

    delete sdlTexture;
    texture = Texture();

    return ResultOk;
}

Result<VertexShader>
SdlGpuDevice::CreateVertexShader(const VertexShaderSpec& shaderSpec)
{
    const std::string_view path = std::get<imstring>(shaderSpec.Source);
    auto shaderResult = LoadShader<GpuVertexShader>(
        Device,
        path,
        shaderSpec.NumUniformBuffers,
        0);

    expect(shaderResult, shaderResult.error());

    GpuVertexShader* gpuShader = new SdlGpuVertexShader(Device, shaderResult.value());
    expect(gpuShader, "Error allocating SDLGPUVertexShader");

    return VertexShader(gpuShader);
}

Result<void>
SdlGpuDevice::DestroyVertexShader(VertexShader& shader)
{
    auto sdlShader = shader.Get<SdlGpuVertexShader>();
    if (!sdlShader)
    {
        return Error("Invalid vertex shader");
    }

    delete sdlShader;
    shader = VertexShader();

    return ResultOk;
}

Result<FragmentShader>
SdlGpuDevice::CreateFragmentShader(const FragmentShaderSpec& shaderSpec)
{
    // All fragment shaders have the same number of samplers.
    static constexpr unsigned numSamplers = 1;

    const std::string_view path = std::get<imstring>(shaderSpec.Source);
    auto shaderResult = LoadShader<GpuFragmentShader>(
        Device,
        path,
        0,
        numSamplers);

    expect(shaderResult, shaderResult.error());

    GpuFragmentShader* gpuShader = new SdlGpuFragmentShader(Device, shaderResult.value());
    expect(gpuShader, "Error allocating SDLGPUFragmentShader");

    return FragmentShader(gpuShader);
}

Result<void>
SdlGpuDevice::DestroyFragmentShader(FragmentShader& shader)
{
    auto sdlShader = shader.Get<SdlGpuFragmentShader>();
    if (!sdlShader)
    {
        return Error("Invalid fragment shader");
    }

    delete sdlShader;
    shader = FragmentShader();

    return ResultOk;
}

Result<RenderGraph*>
SdlGpuDevice::CreateRenderGraph()
{
    SdlRenderGraph* renderGraph = new SdlRenderGraph(this);
    expect(renderGraph, "Error allocating SDLRenderGraph");
    return renderGraph;
}

void SdlGpuDevice::DestroyRenderGraph(RenderGraph* renderGraph)
{
    delete renderGraph;
}

Result<SDL_GPUGraphicsPipeline*>
SdlGpuDevice::GetOrCreatePipeline(const Material& mtl)
{
    SDL_GPUTextureFormat colorTargetFormat = SDL_GetGPUSwapchainTextureFormat(Device, Window);
    PipelineKey key
    {
        .ColorFormat = colorTargetFormat,
        .VertexShader = mtl.VertexShader.Get<SdlGpuVertexShader>()->GetShader(),
        .FragShader = mtl.FragmentShader.Get<SdlGpuFragmentShader>()->GetShader()
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
        .vertex_shader = mtl.VertexShader.Get<SdlGpuVertexShader>()->GetShader(),
        .fragment_shader = mtl.FragmentShader.Get<SdlGpuFragmentShader>()->GetShader(),
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

/// @brief GPU shader stage type for type T.
template <typename T>
constexpr SDL_GPUShaderStage SHADER_STAGE;

template<> constexpr SDL_GPUShaderStage SHADER_STAGE<GpuVertexShader> = SDL_GPU_SHADERSTAGE_VERTEX;
template<> constexpr SDL_GPUShaderStage SHADER_STAGE<GpuFragmentShader> = SDL_GPU_SHADERSTAGE_FRAGMENT;

template<typename T>
Result<SDL_GPUShader*> LoadShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    const unsigned numUniformBuffers,
    const unsigned numSamplers)
{
    expect(fileName.size() > 0, "Invalid shader file name");

    auto fnWithExt = std::string(fileName) + SHADER_EXTENSION;
    size_t fileSize;
    void* shaderSrc = SDL_LoadFile(fnWithExt.data(), &fileSize);
    expect(shaderSrc, "{}: {}", fnWithExt, SDL_GetError());

    auto cleanup = scope_exit([&]()
    {
        SDL_free(shaderSrc);
    });

    SDL_GPUShaderCreateInfo shaderCreateInfo
    {
        .code_size = fileSize,
        .code = (uint8_t*)shaderSrc,
        .entrypoint = "main",
        .format = SHADER_FORMAT,
        .stage = SHADER_STAGE<T>,
        .num_samplers = numSamplers,
        .num_uniform_buffers = numUniformBuffers
    };

    SDL_GPUShader* shader = SDL_CreateGPUShader(gpuDevice, &shaderCreateInfo);
    expect(shader, SDL_GetError());

    return shader;
}

/// @brief Common function to create a GPU buffer from multiple spans.
template<typename T>
static Result<std::tuple<SDL_GPUBuffer*, size_t>>
CreateGpuBuffer(SDL_GPUDevice* gd, const std::span<const std::span<const T>>& spans)
{
    const size_t sizeofBuffer = std::ranges::fold_left(spans, 0, [](size_t sum, const std::span<const T>& span)
    {
        return sum + span.size() * sizeof(T);
    });

    SDL_GPUBufferCreateInfo bufferCreateInfo
    {
        .usage = GpuBufferTraits<T>::Usage,
        .size = static_cast<Uint32>(sizeofBuffer)
    };

    SDL_GPUBuffer* nativeBuf = SDL_CreateGPUBuffer(gd, &bufferCreateInfo);
    expect(nativeBuf, SDL_GetError());

    auto bufCleanup = scope_exit([&]()
    {
        SDL_ReleaseGPUBuffer(gd, nativeBuf);
    });

    //Transfer data to GPU memory.

    //Create transfer buffer
    SDL_GPUTransferBufferCreateInfo xferBufCreateInfo
    {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = static_cast<Uint32>(sizeofBuffer)
    };

    SDL_GPUTransferBuffer* transferBuffer = SDL_CreateGPUTransferBuffer(gd, &xferBufCreateInfo);
    expect(transferBuffer, SDL_GetError());
    auto tbufferCleanup = scope_exit([&]()
    {
        SDL_ReleaseGPUTransferBuffer(gd, transferBuffer);
    });

    // Copy to transfer buffer
    uint8_t* xferBuf = static_cast<uint8_t*>(SDL_MapGPUTransferBuffer(gd, transferBuffer, false));
    expect(xferBuf, SDL_GetError());

    T* dst = reinterpret_cast<T*>(xferBuf);

    for(const auto& span : spans)
    {
        if(span.empty())
        {
            continue;
        }

        const size_t spanLen = span.size();
        const size_t dataSize = spanLen * sizeof(span[0]);
        ::memcpy(dst, span.data(), dataSize);
        dst += spanLen;
    }

    SDL_UnmapGPUTransferBuffer(gd, transferBuffer);

    //Upload data to GPU mem.
    SDL_GPUCommandBuffer* uploadCmdBuf = SDL_AcquireGPUCommandBuffer(gd);
    expect(uploadCmdBuf, SDL_GetError());
    auto cmdBufCleanup = scope_exit([&]()
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
        .buffer = nativeBuf,
        .offset = 0,
        .size = static_cast<Uint32>(sizeofBuffer)
    };

    SDL_UploadToGPUBuffer(copyPass, &srcBuf, &dstBuf, false);

    SDL_EndGPUCopyPass(copyPass);

    expect(SDL_SubmitGPUCommandBuffer(uploadCmdBuf), SDL_GetError());

    cmdBufCleanup.release();
    bufCleanup.release();

    return std::make_tuple(nativeBuf, sizeofBuffer);
}