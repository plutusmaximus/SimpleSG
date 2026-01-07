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
static Result<SDL_GPUBuffer*>
CreateGpuBuffer(SDL_GPUDevice* gd, const std::span<const std::span<const T>>& spans);

SDLGpuIndexBuffer::~SDLGpuIndexBuffer()
{
    // Make sure we only release the buffer if this is not a sub-range buffer.
    if (Buffer && !m_BaseBuffer) { SDL_ReleaseGPUBuffer(m_GpuDevice, Buffer); }
}

Result<RefPtr<GpuIndexBuffer>>
SDLGpuIndexBuffer::GetSubRange(const uint32_t itemOffset, const uint32_t itemCount)
{
    eassert(itemOffset + itemCount <= this->ItemCount);
    auto buf = new SDLGpuIndexBuffer(this, itemOffset, itemCount);
    expect(buf, "Error allocating SDLGpuIndexBuffer");
    return buf;
}

SDLGpuVertexBuffer::~SDLGpuVertexBuffer()
{
    // Make sure we only release the buffer if this is not a sub-range buffer.
    if (Buffer && !m_BaseBuffer) { SDL_ReleaseGPUBuffer(m_GpuDevice, Buffer); }
}

Result<RefPtr<GpuVertexBuffer>>
SDLGpuVertexBuffer::GetSubRange(const uint32_t itemOffset, const uint32_t itemCount)
{
    eassert(itemOffset + itemCount <= this->ItemCount);
    auto buf = new SDLGpuVertexBuffer(this, itemOffset, itemCount);
    expect(buf, "Error allocating SDLGpuVertexBuffer");
    return buf;
}

SDLGpuTexture::~SDLGpuTexture()
{
    if (Texture) { SDL_ReleaseGPUTexture(m_GpuDevice, Texture); }
    // Sampler is released by the SDLGPUDevice destructor.
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

Extent SDLGPUDevice::GetExtent() const
{
    int width = 0, height = 0;
    if(!SDL_GetWindowSizeInPixels(Window, &width, &height))
    {
        logError("Failed to get window size: {}", SDL_GetError());
    }
    return Extent{static_cast<float>(width), static_cast<float>(height)};
}

Result<RefPtr<GpuIndexBuffer>>
SDLGPUDevice::CreateIndexBuffer(const std::span<const VertexIndex>& indices)
{
    std::span<const VertexIndex> spans[]{indices};
    return CreateIndexBuffer(spans);
}

Result<RefPtr<GpuVertexBuffer>>
SDLGPUDevice::CreateVertexBuffer(const std::span<const Vertex>& vertices)
{
    std::span<const Vertex> spans[]{vertices};
    return CreateVertexBuffer(spans);
}

Result<RefPtr<GpuIndexBuffer>>
SDLGPUDevice::CreateIndexBuffer(const std::span<std::span<const VertexIndex>>& indices)
{
    auto nativeBuf = CreateGpuBuffer<VertexIndex>(Device, indices);
    expect(nativeBuf, nativeBuf.error());

    uint32_t count = 0;
    for(const auto& span : indices)
    {
        count += static_cast<uint32_t>(span.size());
    }

    auto ib = new SDLGpuIndexBuffer(
        Device,
        nativeBuf.value(),
        0,
        count);

    if(!ib)
    {
        SDL_ReleaseGPUBuffer(Device, nativeBuf.value());
        return std::unexpected("Error allocating SDLGpuIndexBuffer");
    }

    return ib;
}

Result<RefPtr<GpuVertexBuffer>>
SDLGPUDevice::CreateVertexBuffer(const std::span<std::span<const Vertex>>& vertices)
{
    auto nativeBuf = CreateGpuBuffer<Vertex>(Device, vertices);
    expect(nativeBuf, nativeBuf.error());

    uint32_t count = 0;
    for(const auto& span : vertices)
    {
        count += static_cast<uint32_t>(span.size());
    }

    auto vb = new SDLGpuVertexBuffer(
        Device,
        nativeBuf.value(),
        0,
        count);

    if(!vb)
    {
        SDL_ReleaseGPUBuffer(Device, nativeBuf.value());
        return std::unexpected("Error allocating SDLGpuVertexBuffer");
    }

    return vb;
}

template<class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };

Result<RefPtr<GpuTexture>>
SDLGPUDevice::CreateTexture(const TextureSpec& textureSpec)
{
    eassert(textureSpec.IsValid());
    
    auto acceptor = overloaded
    {
        [this](TextureSpec::None_t)->Result<RefPtr<GpuTexture>> { return std::unexpected("Texture source is not specified"); },
        [this](const std::string& path) { return CreateTexture(path); },
        [this](const Image& image) { return CreateTexture(image); },
        [this](const RgbaColorf& color) { return CreateTexture(color); }
    };

    return std::visit(acceptor, textureSpec.Source);
}

Result<RefPtr<GpuVertexShader>>
SDLGPUDevice::CreateVertexShader(const VertexShaderSpec& shaderSpec)
{
    const std::string_view path = std::get<std::string>(shaderSpec.Source);
    auto shaderResult = LoadShader<GpuVertexShader>(
        Device,
        path,
        3,
        0);

    expect(shaderResult, shaderResult.error());

    RefPtr<GpuVertexShader> gpuShader = new SDLGpuVertexShader(Device, shaderResult.value());
    expect(gpuShader, "Error allocating SDLGPUVertexShader");

    return gpuShader;
}

Result<RefPtr<GpuFragmentShader>>
SDLGPUDevice::CreateFragmentShader(const FragmentShaderSpec& shaderSpec)
{
    const std::string_view path = std::get<std::string>(shaderSpec.Source);
    auto shaderResult = LoadShader<GpuFragmentShader>(
        Device,
        path,
        0,
        1);

    expect(shaderResult, shaderResult.error());

    RefPtr<GpuFragmentShader> gpuShader = new SDLGpuFragmentShader(Device, shaderResult.value());
    expect(gpuShader, "Error allocating SDLGPUFragmentShader");

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
SDLGPUDevice::CreateTexture(const Image& image)
{
    return CreateTexture(image.Width, image.Height, image.Pixels);
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

    RefPtr<SDLGpuTexture> gpuTex = new SDLGpuTexture(Device, texture, m_Sampler);

    expectv(gpuTex, "Error allocating SDLGPUTexture");

    texCleanup.Cancel();

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
static Result<SDL_GPUBuffer*>
CreateGpuBuffer(SDL_GPUDevice* gd, const std::span<const std::span<const T>>& spans)
{
    size_t sizeofBuffer = 0;

    for(const auto& span : spans)
    {
        sizeofBuffer += span.size() * sizeof(span[0]);
    }

    SDL_GPUBufferCreateInfo bufferCreateInfo
    {
        .usage = GpuBufferTraits<T>::Usage,
        .size = static_cast<Uint32>(sizeofBuffer)
    };

    SDL_GPUBuffer* nativeBuf = SDL_CreateGPUBuffer(gd, &bufferCreateInfo);
    expect(nativeBuf, SDL_GetError());

    auto bufCleanup = Finally([&]()
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
    auto tbufferCleanup = Finally([&]()
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
        .buffer = nativeBuf,
        .offset = 0,
        .size = static_cast<Uint32>(sizeofBuffer)
    };

    SDL_UploadToGPUBuffer(copyPass, &srcBuf, &dstBuf, false);

    SDL_EndGPUCopyPass(copyPass);

    expect(SDL_SubmitGPUCommandBuffer(uploadCmdBuf), SDL_GetError());

    cmdBufCleanup.Cancel();
    bufCleanup.Cancel();

    return nativeBuf;
}