#include "DawnGpuDevice.h"

#include "Logging.h"
#include "Material.h"

#include <iostream> //DO NOT SUBMIT
#include <SDL3/SDL.h>
#include <webgpu/webgpu_cpp.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static Result<wgpu::Instance> CreateInstance();

static Result<wgpu::Adapter> CreateAdapter(wgpu::Instance instance);

static Result<wgpu::Device> CreateDevice(wgpu::Instance instance, wgpu::Adapter adapter);

static wgpu::PresentMode ChoosePresentMode(const wgpu::PresentMode* availableModes,
    size_t modeCount);

static wgpu::TextureFormat ChooseBackbufferFormat(const wgpu::TextureFormat* availableFormats,
    size_t formatCount);

static Result<wgpu::Surface> CreateSurface(wgpu::Instance instance, SDL_Window* window);

static Result<wgpu::TextureFormat> ConfigureSurface(wgpu::Adapter adapter,
    wgpu::Device device,
    wgpu::Surface surface,
    const uint32_t width,
    const uint32_t height);

// Helper function to create a GPU buffer from multiple spans of data
template<typename T>
static Result<std::tuple<wgpu::Buffer, size_t>>
CreateGpuBuffer(wgpu::Device device, const std::span<const std::span<const T>>& spans);

DawnGpuDevice::DawnGpuDevice(
    SDL_Window* window, wgpu::Instance instance, wgpu::Adapter adapter, wgpu::Device device, wgpu::Surface surface)
    : Window(window),
      Instance(instance),
      Adapter(adapter),
      Device(device),
      Surface(surface)
{
}

Result<GpuDevice*>
DawnGpuDevice::Create(SDL_Window* window)
{
    logInfo("Creating Dawn GPU Device...");

    auto instanceResult = CreateInstance();
    expect(instanceResult, instanceResult.error());
    auto instance = instanceResult.value();

    auto adapterResult = CreateAdapter(instance);
    expect(adapterResult, adapterResult.error());
    auto adapter = adapterResult.value();

    auto deviceResult = CreateDevice(instance, adapter);
    expect(deviceResult, deviceResult.error());
    auto device = deviceResult.value();

    auto surfaceResult = CreateSurface(instance, window);
    expect(surfaceResult, surfaceResult.error());
    auto surface = surfaceResult.value();

    int width, height;
    SDL_GetWindowSize(window, &width, &height);

    auto configureSurfaceResult = ConfigureSurface(adapter,
        device,
        surface,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));
    expect(configureSurfaceResult, configureSurfaceResult.error());

    DawnGpuDevice* dawnDevice = new DawnGpuDevice(window, instance, adapter, device, surface);

    expectv(dawnDevice, "Error allocating device");

    return dawnDevice;
}

void
DawnGpuDevice::Destroy(GpuDevice* /*device*/)
{
}

DawnGpuDevice::~DawnGpuDevice() = default;

Extent
DawnGpuDevice::GetExtent() const
{
    return Extent(0, 0);
}

Result<GpuVertexBuffer*>
DawnGpuDevice::CreateVertexBuffer(const std::span<const Vertex>& vertices)
{
    std::span<const Vertex> spans[]{vertices};
    return CreateVertexBuffer(spans);
}

Result<GpuVertexBuffer*>
DawnGpuDevice::CreateVertexBuffer(const std::span<std::span<const Vertex>>& vertices)
{
    auto nativeBufResult = CreateGpuBuffer<Vertex>(Device, vertices);
    expect(nativeBufResult, nativeBufResult.error());

    auto [nativeBuf, sizeofBuffer] = nativeBufResult.value();

    GpuResource* res = m_ResourceAllocator.New();

    expectv(res, "Error allocating DawnGpuVertexBuffer");

    return ::new(&res->VertexBuffer)
        DawnGpuVertexBuffer(this, nativeBuf, static_cast<uint32_t>(sizeofBuffer / sizeof(Vertex)));
}

Result<void>
DawnGpuDevice::DestroyVertexBuffer(GpuVertexBuffer* vb)
{
    DawnGpuVertexBuffer* dawnBuffer = static_cast<DawnGpuVertexBuffer*>(vb);
    eassert(this == dawnBuffer->m_GpuDevice,
        "VertexBuffer does not belong to this device");
    dawnBuffer->~DawnGpuVertexBuffer();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(vb));
    return ResultOk;
}

Result<GpuIndexBuffer*>
DawnGpuDevice::CreateIndexBuffer(const std::span<const VertexIndex>& indices)
{
    std::span<const VertexIndex> spans[]{indices};
    return CreateIndexBuffer(spans);
}

Result<GpuIndexBuffer*>
DawnGpuDevice::CreateIndexBuffer(const std::span<std::span<const VertexIndex>>& indices)
{
    auto nativeBufResult = CreateGpuBuffer<VertexIndex>(Device, indices);
    expect(nativeBufResult, nativeBufResult.error());

    auto [nativeBuf, sizeofBuffer] = nativeBufResult.value();

    GpuResource* res = m_ResourceAllocator.New();

    expectv(res, "Error allocating DawnGpuIndexBuffer");

    return ::new(&res->IndexBuffer)
        DawnGpuIndexBuffer(this, nativeBuf, static_cast<uint32_t>(sizeofBuffer / sizeof(VertexIndex)));
}

Result<void>
DawnGpuDevice::DestroyIndexBuffer(GpuIndexBuffer* buffer)
{
    DawnGpuIndexBuffer* dawnBuffer = static_cast<DawnGpuIndexBuffer*>(buffer);
    eassert(this == dawnBuffer->m_GpuDevice,
        "IndexBuffer does not belong to this device");
    dawnBuffer->~DawnGpuIndexBuffer();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(buffer));
    return ResultOk;
}

Result<GpuTexture*>
DawnGpuDevice::CreateTexture(const unsigned width,
    const unsigned height,
    const uint8_t* pixels,
    const unsigned rowStride,
    const imstring& name)
{
    const uint32_t rowPitch = width * 4;
    const uint32_t alignedRowPitch = (rowPitch + 255) & ~255;
    const uint32_t stagingSize = alignedRowPitch * height;

    wgpu::BufferDescriptor bufDesc = //
        {
            .label = name.c_str(),
            .usage = wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::MapWrite,
            .size = stagingSize,
            .mappedAtCreation = true,
        };

    wgpu::Buffer stagingBuffer = Device.CreateBuffer(&bufDesc);
    expect(stagingBuffer, "Failed to create staging buffer of size {} for texture upload",
            stagingSize);

    void *mapped = stagingBuffer.GetMappedRange(0, stagingSize);
    expect(mapped, "Failed to map staging buffer for texture upload");

    uint8_t *dst = (uint8_t *)mapped;
    const uint8_t *src = pixels;
    for(uint32_t y = 0; y < height; ++y, dst += alignedRowPitch, src += rowStride)
    {
        ::memcpy(dst, src, width * 4);
    }

    stagingBuffer.Unmap();

    wgpu::CommandEncoderDescriptor encDesc = { .label = name.c_str() };
    wgpu::CommandEncoder encoder = Device.CreateCommandEncoder(&encDesc);
    expect(encoder, "Failed to create command encoder for texture upload");

    wgpu::TextureDescriptor textureDesc //
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
            .format = wgpu::TextureFormat::RGBA8Unorm,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };

    wgpu::Texture texture = Device.CreateTexture(&textureDesc);
    expect(texture, "Failed to create texture");

    wgpu::TexelCopyBufferInfo copySrc = //
        {
            .layout = //
            {
                .offset = 0,
                .bytesPerRow = alignedRowPitch,
                .rowsPerImage = height,
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
            .width = width,
            .height = height,
            .depthOrArrayLayers = 1,
        };

    encoder.CopyBufferToTexture(&copySrc, &copyDst, &copySize);
    wgpu::CommandBuffer commandBuffer = encoder.Finish();

    expect(commandBuffer, "Failed to finish command buffer for texture upload");

    struct QueueSubmitResult
    {
        std::atomic<bool> done = false;
        std::optional<Error> queueSubmitError;
    };

    QueueSubmitResult result;

    //TODO - change API to separate creating a resource from populating it
    Device.GetQueue().Submit(1, &commandBuffer);
    Device.GetQueue().OnSubmittedWorkDone(
        wgpu::CallbackMode::AllowProcessEvents,
        +[](wgpu::QueueWorkDoneStatus status, wgpu::StringView message, QueueSubmitResult* result)
        {
            if(status != wgpu::QueueWorkDoneStatus::Success)
            {
                result->queueSubmitError =
                    Error("Queue submit failed: {}", std::string(message.data, message.length));
            }
            result->done.store(true);
        },
        &result);

    while(!result.done.load())
    {
        Instance.ProcessEvents();
    }

    if(result.queueSubmitError)
    {
        return result.queueSubmitError.value();
    }

    if(!m_Sampler)
    {
        // Create sampler
        wgpu::SamplerDescriptor samplerDesc //
            {
                .label = "MainSampler",
                .addressModeU = wgpu::AddressMode::Repeat,
                .addressModeV = wgpu::AddressMode::Repeat,
                .addressModeW = wgpu::AddressMode::Undefined,
                .magFilter = wgpu::FilterMode::Linear,
                .minFilter = wgpu::FilterMode::Linear,
                .mipmapFilter = wgpu::MipmapFilterMode::Undefined,
                .lodMinClamp = 0.0f,
                .lodMaxClamp = 32.0f,
                .compare = wgpu::CompareFunction::Undefined,
                .maxAnisotropy = 1,
            };

        m_Sampler = Device.CreateSampler(&samplerDesc);
        expect(m_Sampler, "Failed to create sampler");
    }

    GpuResource* res = m_ResourceAllocator.New();

    expectv(res, "Error allocating DawnGpuTexture");

    return ::new(&res->Texture) DawnGpuTexture(this, texture, m_Sampler);
}

Result<GpuTexture*>
DawnGpuDevice::CreateTexture(const RgbaColorf& color, const imstring& name)
{
    RgbaColoru8 colorU8{color};
    const uint8_t pixelData[4]{colorU8.r, colorU8.g, colorU8.b, colorU8.a};

    return CreateTexture(1, 1, pixelData, 4, name);
}

Result<void>
DawnGpuDevice::DestroyTexture(GpuTexture* texture)
{
    DawnGpuTexture* dawnTexture = static_cast<DawnGpuTexture*>(texture);
    eassert(this == dawnTexture->m_GpuDevice,
        "Texture does not belong to this device");
    dawnTexture->~DawnGpuTexture();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(texture));
    return ResultOk;
}

Result<GpuDepthBuffer*>
DawnGpuDevice::CreateDepthBuffer(const unsigned /*width*/,
    const unsigned /*height*/,
    const float /*clearDepth*/,
    const imstring& /*name*/)
{
    eassert(false, "Not implemented");
    return Result<GpuDepthBuffer*>();
}

Result<void>
DawnGpuDevice::DestroyDepthBuffer(GpuDepthBuffer* /*depthBuffer*/)
{
    eassert(false, "Not implemented");
    return ResultOk;
}

Result<GpuVertexShader*>
DawnGpuDevice::CreateVertexShader(const std::span<const uint8_t>& shaderCode)
{
    wgpu::StringView shaderCodeView{ reinterpret_cast<const char*>(shaderCode.data()),
        shaderCode.size() };
    wgpu::ShaderSourceWGSL wgsl{ { .code = shaderCodeView } };
    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{ .nextInChain = &wgsl };

    wgpu::ShaderModule shaderModule = Device.CreateShaderModule(&shaderModuleDescriptor);
    expect(shaderModule, "Failed to create shader module");

    GpuResource* res = m_ResourceAllocator.New();

    expectv(res, "Error allocating DawnGpuVertexShader");

    return ::new(&res->VertexShader) DawnGpuVertexShader(this, shaderModule);
}

Result<void>
DawnGpuDevice::DestroyVertexShader(GpuVertexShader* shader)
{
    DawnGpuVertexShader* dawnShader = static_cast<DawnGpuVertexShader*>(shader);
    eassert(this == dawnShader->m_GpuDevice,
        "VertexShader does not belong to this device");
    dawnShader->~DawnGpuVertexShader();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(shader));
    return ResultOk;
}

Result<GpuFragmentShader*>
DawnGpuDevice::CreateFragmentShader(const std::span<const uint8_t>& shaderCode)
{
    wgpu::StringView shaderCodeView{ reinterpret_cast<const char*>(shaderCode.data()),
        shaderCode.size() };
    wgpu::ShaderSourceWGSL wgsl{ { .code = shaderCodeView } };
    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{ .nextInChain = &wgsl };

    wgpu::ShaderModule shaderModule = Device.CreateShaderModule(&shaderModuleDescriptor);
    expect(shaderModule, "Failed to create shader module");

    GpuResource* res = m_ResourceAllocator.New();

    expectv(res, "Error allocating DawnGpuFragmentShader");

    return ::new(&res->FragmentShader) DawnGpuFragmentShader(this, shaderModule);
}

Result<void>
DawnGpuDevice::DestroyFragmentShader(GpuFragmentShader* shader)
{
    DawnGpuFragmentShader* dawnShader = static_cast<DawnGpuFragmentShader*>(shader);
    eassert(this == dawnShader->m_GpuDevice,
        "FragmentShader does not belong to this device");
    dawnShader->~DawnGpuFragmentShader();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(shader));
    return ResultOk;
}

Result<GpuPipeline*>
DawnGpuDevice::CreatePipeline(const GpuPipelineType /*pipelineType*/,
    GpuVertexShader* /*vertexShader*/,
    GpuFragmentShader* /*fragmentShader*/)
{
    eassert(false, "Not implemented");
    return Result<GpuPipeline*>();
}

Result<void>
DawnGpuDevice::DestroyPipeline(GpuPipeline* /*pipeline*/)
{
    eassert(false, "Not implemented");
    /*DawnGpuPipeline* dawnPipeline = static_cast<DawnGpuPipeline*>(pipeline);
    eassert(this == dawnPipeline->m_GpuDevice,
        "Pipeline does not belong to this device");
    dawnPipeline->~DawnGpuPipeline();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(pipeline));*/
    return ResultOk;
}

Result<GpuRenderPass*>
DawnGpuDevice::CreateRenderPass(const GpuRenderPassType /*renderPassType*/)
{
    eassert(false, "Not implemented");
    return Result<GpuRenderPass*>();
}

Result<void>
DawnGpuDevice::DestroyRenderPass(GpuRenderPass* /*renderPass*/)
{
    eassert(false, "Not implemented");
    /*DawnGpuRenderPass* dawnRenderPass = static_cast<DawnGpuRenderPass*>(renderPass);
    eassert(this == dawnRenderPass->m_GpuDevice,
        "RenderPass does not belong to this device");
    dawnRenderPass->~DawnGpuRenderPass();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(renderPass));*/
    return ResultOk;
}

Result<Renderer*>
DawnGpuDevice::CreateRenderer(GpuPipeline* /*pipeline*/)
{
    eassert(false, "Not implemented");
    return Result<Renderer*>();
}

void
DawnGpuDevice::DestroyRenderer(Renderer* /*renderer*/)
{
    eassert(false, "Not implemented");
}

static Result<wgpu::Instance>
CreateInstance()
{
    static const auto kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
    wgpu::InstanceDescriptor instanceDesc //
        {
            .requiredFeatureCount = 1,
            .requiredFeatures = &kTimedWaitAny,
        };
    wgpu::Instance instance = wgpu::CreateInstance(&instanceDesc);

    expect(instance, "Failed to create WGPUInstance");

    return instance;
}

static Result<wgpu::Adapter>
CreateAdapter(wgpu::Instance instance)
{
    static const auto kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;

    Result<wgpu::Adapter> result;

    auto rqstAdapterCb = [&result](wgpu::RequestAdapterStatus status,
                             wgpu::Adapter receivedAdapter,
                             wgpu::StringView message) -> void
    {
        if(status != wgpu::RequestAdapterStatus::Success)
        {
            result = Error("RequestAdapter failed: {}", std::string(message.data, message.length));
        }
        else
        {
            result = std::move(receivedAdapter);
        }
    };

    wgpu::RequestAdapterOptions options //
        {
            .nextInChain = nullptr,
            .powerPreference = wgpu::PowerPreference::HighPerformance,
            .forceFallbackAdapter = false,
            .compatibleSurface = nullptr,
        };

    wgpu::Future fut =
        instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly, rqstAdapterCb);

    wgpu::WaitStatus waitStatus = instance.WaitAny(fut, UINT64_MAX);

    expect(waitStatus == wgpu::WaitStatus::Success,
        "Failed to create WGPUAdapter - WaitAny failed");

    return result;
}

static Result<wgpu::Device>
CreateDevice(wgpu::Instance instance, wgpu::Adapter adapter)
{
    // TODO(KB) - handle device lost.
    auto deviceLostCb = [](const wgpu::Device& device [[maybe_unused]],
                            wgpu::DeviceLostReason reason,
                            wgpu::StringView message)
    {
        logError("Device lost (reason:{}): {}",
            static_cast<int>(reason),
            std::string(message.data, message.length));

        // CallbackCancelled indicates intentional device destruction.
        // exit() was probably already called, or program terminated normally.
        if(wgpu::DeviceLostReason::CallbackCancelled != reason)
        {
            exit(0);
        }
    };

    auto uncapturedErrorCb = [](const wgpu::Device& device [[maybe_unused]],
                                 wgpu::ErrorType errorType,
                                 wgpu::StringView message)
    {
        logError("Uncaptured error (type:{}): {}",
            static_cast<int>(errorType),
            std::string(message.data, message.length));
    };

    wgpu::DeviceDescriptor deviceDesc{ { .label = "MainDevice" } };
    deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents, deviceLostCb);
    deviceDesc.SetUncapturedErrorCallback(uncapturedErrorCb);

    Result<wgpu::Device> result;

    auto rqstDeviceCb = [&result](wgpu::RequestDeviceStatus status,
                            wgpu::Device receivedDevice,
                            wgpu::StringView message) -> void
    {
        if(status != wgpu::RequestDeviceStatus::Success)
        {
            result = Error("RequestDevice failed: {}", std::string(message.data, message.length));
        }
        else
        {
            result = std::move(receivedDevice);
        }
    };

    wgpu::Future fut =
        adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly, rqstDeviceCb);

    wgpu::WaitStatus waitStatus = instance.WaitAny(fut, UINT64_MAX);

    expect(waitStatus == wgpu::WaitStatus::Success, "Failed to create WGPUDevice - WaitAny failed");

    return result;
}

static wgpu::PresentMode
ChoosePresentMode(const wgpu::PresentMode* availableModes, size_t modeCount)
{
    wgpu::PresentMode presentMode = wgpu::PresentMode::Undefined;
    for(size_t i = 0; i < modeCount; ++i)
    {
        // Prefer mailbox if available
        if(presentMode == wgpu::PresentMode::Undefined || presentMode == wgpu::PresentMode::Fifo)
        {
            switch(availableModes[i])
            {
                case wgpu::PresentMode::Fifo:
                    presentMode = wgpu::PresentMode::Fifo;
                    break;
                case wgpu::PresentMode::Mailbox:
                    presentMode = wgpu::PresentMode::Mailbox;
                    break;
                default:
                    break;
            }
        }
    }
    return presentMode;
}

static wgpu::TextureFormat
ChooseBackbufferFormat(const wgpu::TextureFormat* availableFormats, size_t formatCount)
{
    // Prefer BGRA8Unorm if available
    for(size_t i = 0; i < formatCount; ++i)
    {
        if(availableFormats[i] == wgpu::TextureFormat::BGRA8Unorm ||
            availableFormats[i] == wgpu::TextureFormat::RGBA8Unorm)
        {
            return availableFormats[i];
        }
    }
    // Fallback to first available format
    return availableFormats[0];
}

#if defined(__EMSCRIPTEN__)

static Result<wgpu::Surface>
CreateSurface(wgpu::Instance instance, SDL_Window* window)
{
    wgpu::EmscriptenSurfaceSourceCanvasHTMLSelector canvas_desc = {};
    canvas_desc.selector = "#canvas";

    wgpu::SurfaceDescriptor surface_desc = {};
    surface_desc.nextInChain = &canvas_desc;
    wgpu::Surface surface = instance.CreateSurface(&surface_desc);

    expect(surface, "Failed to create WGPUSurface from SDL window");

    return surface;
}

#else

static Result<wgpu::Surface>
CreateSurface(wgpu::Instance instance, SDL_Window* window)
{
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    void* hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

    WGPUSurfaceDescriptor surface_descriptor = {};
    WGPUSurface rawSurface = {};

    WGPUSurfaceSourceWindowsHWND surface_src_hwnd = {};
    surface_src_hwnd.chain.sType = WGPUSType_SurfaceSourceWindowsHWND;
    surface_src_hwnd.hinstance = ::GetModuleHandle(NULL);
    surface_src_hwnd.hwnd = hwnd;
    surface_descriptor.nextInChain = &surface_src_hwnd.chain;
    rawSurface = wgpuInstanceCreateSurface(instance.Get(), &surface_descriptor);

    wgpu::Surface surface = wgpu::Surface::Acquire(rawSurface);

    expect(surface, "Failed to create WGPUSurface from SDL window");

    return surface;
}

#endif // defined(__EMSCRIPTEN__)

static Result<wgpu::TextureFormat>
ConfigureSurface(wgpu::Adapter adapter,
    wgpu::Device device,
    wgpu::Surface surface,
    const uint32_t width,
    const uint32_t height)
{
    wgpu::SurfaceCapabilities capabilities;
    expect(surface.GetCapabilities(adapter, &capabilities), "surface.GetCapabilities failed");

    wgpu::PresentMode presentMode =
        ChoosePresentMode(capabilities.presentModes, capabilities.presentModeCount);
    expect(presentMode != wgpu::PresentMode::Undefined, "No supported present mode found");

    wgpu::TextureFormat format =
        ChooseBackbufferFormat(capabilities.formats, capabilities.formatCount);
    expect(format != wgpu::TextureFormat::Undefined, "No supported backbuffer format found");

    wgpu::SurfaceConfiguration config //
        {
            .device = device,
            .format = format,
            .usage = wgpu::TextureUsage::RenderAttachment,
            .width = width,
            .height = height,
            .alphaMode = wgpu::CompositeAlphaMode::Opaque,
            .presentMode = presentMode,
        };

    surface.Configure(&config);

    return format;
}

/// @brief Common function to create a GPU buffer from multiple spans.
template<typename T>
static Result<std::tuple<wgpu::Buffer, size_t>>
CreateGpuBuffer(wgpu::Device device, const std::span<const std::span<const T>>& spans)
{
    const size_t sizeofBuffer = std::ranges::fold_left(spans, 0, [](size_t sum, const std::span<const T>& span)
    {
        return sum + span.size() * sizeof(T);
    });

    wgpu::BufferDescriptor bufferDesc //
        {
            .label = "VertexBuffer",
            .usage = wgpu::BufferUsage::Vertex,
            .size = sizeofBuffer,
            .mappedAtCreation = true,
        };

    wgpu::Buffer buffer = device.CreateBuffer(&bufferDesc);
    expect(buffer, "Failed to create vertex buffer");

    void *p = buffer.GetMappedRange(0,  sizeofBuffer);
    expect(p, "Failed to map vertex buffer");

    T* dst = reinterpret_cast<T*>(p);

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

    buffer.Unmap();

    return std::make_tuple(buffer, sizeofBuffer);
}