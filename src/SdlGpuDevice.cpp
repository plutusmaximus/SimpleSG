#define __LOGGER_NAME__ "SDL "

#include "SdlGpuDevice.h"

#include "Logging.h"

#include "SdlRenderer.h"
#include "scope_exit.h"

#include "Stopwatch.h"

#include <algorithm>
#include <SDL3/SDL.h>
#include <new>

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

static constexpr SDL_GPUTextureFormat kTextureFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
static constexpr SDL_GPUTextureFormat kColorTargetFormat = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
static constexpr SDL_GPUTextureFormat kDepthTargetFormat = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

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
    SDL_ReleaseGPUBuffer(m_GpuDevice->Device, m_Buffer);
}

SdlGpuIndexBuffer::~SdlGpuIndexBuffer()
{
    SDL_ReleaseGPUBuffer(m_GpuDevice->Device, m_Buffer);
}

SdlGpuTexture::~SdlGpuTexture()
{
    if (m_Texture) { SDL_ReleaseGPUTexture(m_GpuDevice->Device, m_Texture); }
    // Sampler is released by the SDLGPUDevice destructor.
}

SdlGpuColorTarget::~SdlGpuColorTarget()
{
    if (m_ColorTarget) { SDL_ReleaseGPUTexture(m_GpuDevice->Device, m_ColorTarget); }
}

SdlGpuDepthTarget::~SdlGpuDepthTarget()
{
    if (m_DepthTarget) { SDL_ReleaseGPUTexture(m_GpuDevice->Device, m_DepthTarget); }
}

SdlGpuVertexShader::~SdlGpuVertexShader()
{
    if (m_Shader) { SDL_ReleaseGPUShader(m_GpuDevice->Device, m_Shader); }
}

SdlGpuFragmentShader::~SdlGpuFragmentShader()
{
    if (m_Shader) { SDL_ReleaseGPUShader(m_GpuDevice->Device, m_Shader); }
}

SdlGpuPipeline::~SdlGpuPipeline()
{
    if (m_Pipeline) { SDL_ReleaseGPUGraphicsPipeline(m_GpuDevice->Device, m_Pipeline); }
};

SdlGpuDevice::SdlGpuDevice(SDL_Window* window, SDL_GPUDevice* gpuDevice)
    : Window(window)
    , Device(gpuDevice)
{
}

#define VK_MAKE_VERSION(major, minor, patch) \
    ((((uint32_t)(major)) << 22U) | (((uint32_t)(minor)) << 12U) | ((uint32_t)(patch)))

Result<GpuDevice*>
SdlGpuDevice::Create(SDL_Window* window)
{
    logInfo("Creating SDL GPU Device...");

    //TODO - move these to environment variables.
    expect(SDL_SetHint(SDL_HINT_RENDER_VULKAN_DEBUG, "1"), SDL_GetError());
    expect(SDL_SetHint(SDL_HINT_RENDER_GPU_DEBUG, "1"), SDL_GetError());

    SDL_GPUVulkanOptions vulkanOptions =
    {
        .vulkan_api_version = VK_MAKE_VERSION(1, 3, 0),
        // Other fields can be set as needed
    };

    // Initialize GPU device
    const bool debugMode = true;

    SDL_PropertiesID props = SDL_CreateProperties();
    expect(props, SDL_GetError());

    auto propsCleanup = scope_exit([&]() { SDL_DestroyProperties(props); });

    expect(SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_SHADERS_SPIRV_BOOLEAN, true),
        SDL_GetError());
    expect(SDL_SetBooleanProperty(props, SDL_PROP_GPU_DEVICE_CREATE_DEBUGMODE_BOOLEAN, debugMode),
        SDL_GetError());
    expect(SDL_SetStringProperty(props, SDL_PROP_GPU_DEVICE_CREATE_NAME_STRING, DRIVER_NAME),
        SDL_GetError());
    expect(
        SDL_SetPointerProperty(props, SDL_PROP_GPU_DEVICE_CREATE_VULKAN_OPTIONS_POINTER, &vulkanOptions),
        SDL_GetError());

    SDL_GPUDevice* sdlDevice = SDL_CreateGPUDeviceWithProperties(props);
    expect(sdlDevice, SDL_GetError());

    auto sdlDeviceCleanup = scope_exit([sdlDevice]()
    {
        SDL_DestroyGPUDevice(sdlDevice);
    });

    if (!SDL_ClaimWindowForGPUDevice(sdlDevice, window))
    {
        return Error(SDL_GetError());
    }

    if(SDL_WindowSupportsGPUPresentMode(sdlDevice, window, SDL_GPU_PRESENTMODE_MAILBOX))
    {
        logInfo("Using SDL_GPU_PRESENTMODE_MAILBOX present mode.");

        if(!SDL_SetGPUSwapchainParameters(sdlDevice,
               window,
               SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
               SDL_GPU_PRESENTMODE_MAILBOX))
        {
            return Error(SDL_GetError());
        }
    }
    else if(SDL_WindowSupportsGPUPresentMode(sdlDevice, window, SDL_GPU_PRESENTMODE_VSYNC))
    {
        logInfo("Using SDL_GPU_PRESENTMODE_VSYNC present mode.");

        if(!SDL_SetGPUSwapchainParameters(sdlDevice,
               window,
               SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
               SDL_GPU_PRESENTMODE_VSYNC))
        {
            return Error(SDL_GetError());
        }
    }
    else
    {
        return Error("No supported present mode found for window.");
    }

    SdlGpuDevice* device = new SdlGpuDevice(window, sdlDevice);

    expectv(device, "Error allocating device");

    sdlDeviceCleanup.release();

    return device;
}

void SdlGpuDevice::Destroy(GpuDevice* device)
{
    delete static_cast<SdlGpuDevice*>(device);
}

SdlGpuDevice::~SdlGpuDevice()
{
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

Result<GpuVertexBuffer*>
SdlGpuDevice::CreateVertexBuffer(const std::span<const Vertex>& vertices)
{
    std::span<const Vertex> spans[]{vertices};
    return CreateVertexBuffer(spans);
}

Result<GpuVertexBuffer*>
SdlGpuDevice::CreateVertexBuffer(const std::span<std::span<const Vertex>>& vertices)
{
    auto nativeBufResult = CreateGpuBuffer<Vertex>(Device, vertices);
    expect(nativeBufResult, nativeBufResult.error());

    auto [nativeBuf, sizeofBuffer] = nativeBufResult.value();

    GpuResource* res = m_ResourceAllocator.New();

    if(!res)
    {
        SDL_ReleaseGPUBuffer(Device, nativeBuf);
        return Error("Error allocating GpuResource");
    }

    return ::new(&res->VertexBuffer)SdlGpuVertexBuffer(this,
        nativeBuf,
        static_cast<uint32_t>(sizeofBuffer / sizeof(Vertex)));
}

Result<void>
SdlGpuDevice::DestroyVertexBuffer(GpuVertexBuffer* buffer)
{
    SdlGpuVertexBuffer* sdlBuffer = static_cast<SdlGpuVertexBuffer*>(buffer);
    eassert(this == sdlBuffer->m_GpuDevice,
        "Buffer does not belong to this device");

    sdlBuffer->~SdlGpuVertexBuffer();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(buffer));
    return Result<void>::Success;
}

Result<GpuIndexBuffer*>
SdlGpuDevice::CreateIndexBuffer(const std::span<const VertexIndex>& indices)
{
    std::span<const VertexIndex> spans[]{indices};
    return CreateIndexBuffer(spans);
}

Result<GpuIndexBuffer*>
SdlGpuDevice::CreateIndexBuffer(const std::span<std::span<const VertexIndex>>& indices)
{
    auto nativeBufResult = CreateGpuBuffer<VertexIndex>(Device, indices);
    expect(nativeBufResult, nativeBufResult.error());

    auto [nativeBuf, sizeofBuffer] = nativeBufResult.value();

    GpuResource* res = m_ResourceAllocator.New();

    if(!res)
    {
        SDL_ReleaseGPUBuffer(Device, nativeBuf);
        return Error("Error allocating GpuResource");
    }

    return ::new(&res->IndexBuffer)SdlGpuIndexBuffer(this,
        nativeBuf,
        static_cast<uint32_t>(sizeofBuffer / sizeof(VertexIndex)));
}

Result<void>
SdlGpuDevice::DestroyIndexBuffer(GpuIndexBuffer* buffer)
{
    SdlGpuIndexBuffer* sdlBuffer = static_cast<SdlGpuIndexBuffer*>(buffer);
    eassert(this == sdlBuffer->m_GpuDevice,
        "Buffer does not belong to this device");

    sdlBuffer->~SdlGpuIndexBuffer();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(buffer));
    return Result<void>::Success;
}

Result<GpuTexture*>
SdlGpuDevice::CreateTexture(const unsigned width,
    const unsigned height,
    const uint8_t* pixels,
    const unsigned rowStride,
    const imstring& name)
{
    Stopwatch sw1;

    SDL_PropertiesID props = SDL_CreateProperties();
    expect(props, SDL_GetError());

    auto propsCleanup = scope_exit([&]() { SDL_DestroyProperties(props); });

    expect(SDL_SetStringProperty(props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, name.c_str()),
        SDL_GetError());

    // Create GPU texture
    SDL_GPUTextureCreateInfo textureInfo = //
        {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = kTextureFormat,
            .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = width,
            .height = height,
            .layer_count_or_depth = 1,
            .num_levels = 1,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = props,
        };

    Stopwatch sw;

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(Device, &textureInfo);
    expect(texture, SDL_GetError());
    auto texCleanup = scope_exit([&]() { SDL_ReleaseGPUTexture(Device, texture); });

    logDebug("SDL_CreateGPUTexture: {} ms", sw.Elapsed() * 1000.0f);
    sw.Mark();

    const unsigned sizeofData = textureInfo.width * textureInfo.height * 4;

    SDL_GPUTransferBufferCreateInfo xferBufferCreateInfo{ .usage =
                                                              SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeofData };

    // Create transfer buffer for uploading pixel data
    SDL_GPUTransferBuffer* transferBuffer =
        SDL_CreateGPUTransferBuffer(Device, &xferBufferCreateInfo);
    expect(transferBuffer, SDL_GetError());
    auto tbufferCleanup =
        scope_exit([&]() { SDL_ReleaseGPUTransferBuffer(Device, transferBuffer); });

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
    for(unsigned row = 0; row < textureInfo.height;
        ++row, srcPixels += rowStride, dstPixels += dstRowBytes)
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
    auto cmdBufCleanup = scope_exit([&]() { SDL_CancelGPUCommandBuffer(cmdBuffer); });

    logDebug("SDL_AcquireGPUCommandBuffer: {} ms", sw.Elapsed() * 1000.0f);
    sw.Mark();

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuffer);
    expect(copyPass, SDL_GetError());

    logDebug("SDL_BeginGPUCopyPass: {} ms", sw.Elapsed() * 1000.0f);
    sw.Mark();

    SDL_GPUTextureTransferInfo transferInfo //
        {
            .transfer_buffer = transferBuffer,
            .offset = 0,
            .pixels_per_row = textureInfo.width,
            .rows_per_layer = textureInfo.height,
        };

    SDL_GPUTextureRegion textureRegion //
        {
            .texture = texture,
            .w = textureInfo.width,
            .h = textureInfo.height,
            .d = 1,
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

    auto samplerResult = GetDefaultSampler();
    expect(samplerResult, samplerResult.error());

    GpuResource* res = m_ResourceAllocator.New();

    expectv(res, "Error allocating GpuResource");

    texCleanup.release();

    GpuTexture* gpuTex =
        ::new(&res->Texture) SdlGpuTexture(this, texture, samplerResult.value(), width, height);

    logDebug("SdlGpuDevice::CreateTexture: {} ms", sw1.Elapsed() * 1000.0f);

    return gpuTex;
}

Result<GpuTexture*>
SdlGpuDevice::CreateTexture(const RgbaColorf& color, const imstring& name)
{
    RgbaColoru8 colorU8{color};
    const uint8_t pixelData[4]{colorU8.r, colorU8.g, colorU8.b, colorU8.a};

    return CreateTexture(1, 1, pixelData, 4, name);
}

Result<void>
SdlGpuDevice::DestroyTexture(GpuTexture* texture)
{
    SdlGpuTexture* sdlTexture = static_cast<SdlGpuTexture*>(texture);
    eassert(this == sdlTexture->m_GpuDevice,
        "Texture does not belong to this device");
    sdlTexture->~SdlGpuTexture();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(texture));
    return Result<void>::Success;
}

Result<GpuColorTarget*>
SdlGpuDevice::CreateColorTarget(const unsigned width, const unsigned height, const imstring& name)
{
    SDL_PropertiesID props = SDL_CreateProperties();
    expect(props, SDL_GetError());

    auto propsCleanup = scope_exit([&]() { SDL_DestroyProperties(props); });

    expect(SDL_SetStringProperty(props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, name.c_str()),
        SDL_GetError());

    // Create GPU texture
    SDL_GPUTextureCreateInfo textureInfo = //
        {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = kColorTargetFormat,
            .usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER,
            .width = width,
            .height = height,
            .layer_count_or_depth = 1,
            .num_levels = 1,
            .sample_count = SDL_GPU_SAMPLECOUNT_1,
            .props = props,
        };

    SDL_GPUTexture* texture = SDL_CreateGPUTexture(Device, &textureInfo);
    expect(texture, SDL_GetError());

    auto samplerResult = GetDefaultSampler();
    expect(samplerResult, samplerResult.error());

    GpuResource* res = m_ResourceAllocator.New();

    if(!res)
    {
        SDL_ReleaseGPUTexture(Device, texture);
        return Error("Error allocating GpuResource");
    }

    expectv(res, "Error allocating GpuResource");

    GpuColorTarget* gpuColorTarget = ::new(&res->ColorTarget)
        SdlGpuColorTarget(this, texture, samplerResult.value(), width, height);

    return gpuColorTarget;
}

Result<void>
SdlGpuDevice::DestroyColorTarget(GpuColorTarget* colorTarget)
{
    SdlGpuColorTarget* sdlColorTarget = static_cast<SdlGpuColorTarget*>(colorTarget);
    eassert(this == sdlColorTarget->m_GpuDevice,
        "Color target does not belong to this device");
    sdlColorTarget->~SdlGpuColorTarget();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(colorTarget));
    return Result<void>::Success;
}

Result<GpuDepthTarget*>
SdlGpuDevice::CreateDepthTarget(
    const unsigned width, const unsigned height, const imstring& name)
{
    auto props = SDL_CreateProperties();
    expect(props, SDL_GetError());

    auto propsCleanup = scope_exit([&]() { SDL_DestroyProperties(props); });

    expect(SDL_SetStringProperty(props, SDL_PROP_GPU_TEXTURE_CREATE_NAME_STRING, name.c_str()),
        SDL_GetError());

    SDL_GPUTextureCreateInfo m_DepthCreateInfo //
        {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = kDepthTargetFormat,
            .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
            .width = width,
            .height = height,
            .layer_count_or_depth = 1,
            .num_levels = 1,
            .props = props,
        };

    // Avoid D3D12 warning about not specifying clear depth.
    /*SDL_SetFloatProperty(m_DepthCreateInfo.props,
        SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_DEPTH_FLOAT,
        clearDepth);*/

    auto depthBuffer = SDL_CreateGPUTexture(Device, &m_DepthCreateInfo);
    expect(depthBuffer, SDL_GetError());

    auto depthBufferCleanup = scope_exit([&]() { SDL_ReleaseGPUTexture(Device, depthBuffer); });

    GpuResource* res = m_ResourceAllocator.New();

    expectv(res, "Error allocating GpuResource");

    depthBufferCleanup.release();

    return ::new(&res->Texture) SdlGpuDepthTarget(this, depthBuffer, width, height);
}

Result<void>
SdlGpuDevice::DestroyDepthTarget(GpuDepthTarget* depthTarget)
{
    SdlGpuDepthTarget* sdlDepthTarget = static_cast<SdlGpuDepthTarget*>(depthTarget);
    eassert(this == sdlDepthTarget->m_GpuDevice,
        "Depth target does not belong to this device");
    sdlDepthTarget->~SdlGpuDepthTarget();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(depthTarget));
    return Result<void>::Success;
}

Result<GpuVertexShader*>
SdlGpuDevice::CreateVertexShader(const std::span<const uint8_t>& shaderCode)
{
    SDL_GPUShaderCreateInfo shaderCreateInfo
    {
        .code_size = shaderCode.size(),
        .code = shaderCode.data(),
        .entrypoint = "main",
        .format = SHADER_FORMAT,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0,
        .num_uniform_buffers = 3
    };

    SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &shaderCreateInfo);
    expect(shader, SDL_GetError());

    auto shaderCleanup = scope_exit([&]()
    {
        SDL_ReleaseGPUShader(Device, shader);
    });

    GpuResource* res = m_ResourceAllocator.New();

    expectv(res, "Error allocating GpuResource");

    shaderCleanup.release();

    return ::new(&res->VertexShader) SdlGpuVertexShader(this, shader);
}

Result<void>
SdlGpuDevice::DestroyVertexShader(GpuVertexShader* shader)
{
    SdlGpuVertexShader* sdlShader = static_cast<SdlGpuVertexShader*>(shader);
    eassert(this == sdlShader->m_GpuDevice,
        "Shader does not belong to this device");
    sdlShader->~SdlGpuVertexShader();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(shader));
    return Result<void>::Success;
}

Result<GpuFragmentShader*>
SdlGpuDevice::CreateFragmentShader(const std::span<const uint8_t>& shaderCode)
{
    SDL_GPUShaderCreateInfo shaderCreateInfo
    {
        .code_size = shaderCode.size(),
        .code = shaderCode.data(),
        .entrypoint = "main",
        .format = SHADER_FORMAT,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,
        .num_uniform_buffers = 0
    };

    SDL_GPUShader* shader = SDL_CreateGPUShader(Device, &shaderCreateInfo);
    expect(shader, SDL_GetError());

    auto shaderCleanup = scope_exit([&]()
    {
        SDL_ReleaseGPUShader(Device, shader);
    });

    GpuResource* res = m_ResourceAllocator.New();

    expectv(res, "Error allocating GpuResource");

    shaderCleanup.release();

    return ::new(&res->FragmentShader) SdlGpuFragmentShader(this, shader);
}

Result<void>
SdlGpuDevice::DestroyFragmentShader(GpuFragmentShader* shader)
{
    SdlGpuFragmentShader* sdlShader = static_cast<SdlGpuFragmentShader*>(shader);
    eassert(this == sdlShader->m_GpuDevice,
        "Shader does not belong to this device");
    sdlShader->~SdlGpuFragmentShader();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(shader));
    return Result<void>::Success;
}

Result<GpuPipeline*>
SdlGpuDevice::CreatePipeline(const GpuPipelineType pipelineType,
    GpuVertexShader* vertexShader,
    GpuFragmentShader* fragmentShader)
{
    expectv(pipelineType == GpuPipelineType::Opaque,
        "Only opaque pipelines are supported for now.");

    SDL_GPUVertexBufferDescription vertexBufDescriptions[1] = //
        {
            {
                .slot = 0,
                .pitch = sizeof(Vertex),
                .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            },
        };
    SDL_GPUVertexAttribute vertexAttributes[] = //
        {
            { .location = 0,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                .offset = offsetof(Vertex, pos) },
            { .location = 1,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                .offset = offsetof(Vertex, normal) },
            { .location = 2,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                .offset = offsetof(Vertex, uvs[0]) },
        };

    SDL_GPUColorTargetDescription colorTargetDesc//
    {
        .format = kColorTargetFormat,
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
        },
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo //
        {
            .vertex_shader = static_cast<SdlGpuVertexShader*>(vertexShader)->GetShader(),
            .fragment_shader = static_cast<SdlGpuFragmentShader*>(fragmentShader)->GetShader(),
            .vertex_input_state = //
            {
                .vertex_buffer_descriptions = vertexBufDescriptions,
                .num_vertex_buffers = 1,
                .vertex_attributes = vertexAttributes,
                .num_vertex_attributes = std::size(vertexAttributes),
            },
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = //
            {
                .fill_mode = SDL_GPU_FILLMODE_FILL,
                .cull_mode = SDL_GPU_CULLMODE_BACK,
                .front_face = SDL_GPU_FRONTFACE_CLOCKWISE,
                .enable_depth_clip = true,
            },
            .depth_stencil_state = //
            {
                .compare_op = SDL_GPU_COMPAREOP_LESS,
                .enable_depth_test = true,
                .enable_depth_write = true,
            },
            .target_info = //
            {
                .color_target_descriptions = &colorTargetDesc,
                .num_color_targets = 1,
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                .has_depth_stencil_target = true,
            },
        };

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(Device, &pipelineCreateInfo);
    expect(pipeline, SDL_GetError());

    auto pipelineCleanup = scope_exit([&]()
    {
        SDL_ReleaseGPUGraphicsPipeline(Device, pipeline);
    });

    GpuResource* res = m_ResourceAllocator.New();

    expectv(res, "Error allocating GpuResource");

    pipelineCleanup.release();

    return ::new(&res->Pipeline) SdlGpuPipeline(this, pipeline);
}

Result<void>
SdlGpuDevice::DestroyPipeline(GpuPipeline* pipeline)
{
    SdlGpuPipeline* sdlPipeline = static_cast<SdlGpuPipeline*>(pipeline);
    eassert(this == sdlPipeline->m_GpuDevice,
        "Pipeline does not belong to this device");
    sdlPipeline->~SdlGpuPipeline();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(pipeline));
    return Result<void>::Success;
}

Result<Renderer*>
SdlGpuDevice::CreateRenderer(GpuPipeline* pipeline)
{
    SdlRenderer* renderer = m_RendererAllocator.New(this, pipeline);
    expect(renderer, "Error allocating SdlRenderer");
    return renderer;
}

void SdlGpuDevice::DestroyRenderer(Renderer* renderer)
{
    SdlRenderer* sdlRenderer = static_cast<SdlRenderer*>(renderer);
    eassert(this == sdlRenderer->m_GpuDevice,
        "Renderer does not belong to this device");
    m_RendererAllocator.Delete(sdlRenderer);
}

//private:

Result<SDL_GPUSampler*>
SdlGpuDevice::GetDefaultSampler()
{
    if(!m_Sampler)
    {
        // Create sampler
        SDL_GPUSamplerCreateInfo samplerInfo = //
            {
                .min_filter = SDL_GPU_FILTER_LINEAR,
                .mag_filter = SDL_GPU_FILTER_LINEAR,
                .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
                .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            };

        m_Sampler = SDL_CreateGPUSampler(Device, &samplerInfo);
        expect(m_Sampler, SDL_GetError());
    }
    return m_Sampler;
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