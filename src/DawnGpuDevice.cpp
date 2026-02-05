#include "DawnGpuDevice.h"

#include "Logging.h"

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

Result<VertexBuffer>
DawnGpuDevice::CreateVertexBuffer(const std::span<const Vertex>& vertices)
{
    std::span<const Vertex> spans[]{vertices};
    return CreateVertexBuffer(spans);
}

Result<VertexBuffer>
DawnGpuDevice::CreateVertexBuffer(const std::span<std::span<const Vertex>>& vertices)
{
    auto nativeBufResult = CreateGpuBuffer<Vertex>(Device, vertices);
    expect(nativeBufResult, nativeBufResult.error());

    auto [nativeBuf, sizeofBuffer] = nativeBufResult.value();

    auto vb = new DawnGpuVertexBuffer(nativeBuf);
    expect(vb, "Error allocating DawnGpuVertexBuffer");

    const uint32_t count = static_cast<uint32_t>(sizeofBuffer / sizeof(Vertex));

    return VertexBuffer(std::unique_ptr<DawnGpuVertexBuffer>(vb), count);
}

Result<IndexBuffer>
DawnGpuDevice::CreateIndexBuffer(const std::span<const VertexIndex>& indices)
{
    std::span<const VertexIndex> spans[]{indices};
    return CreateIndexBuffer(spans);
}

Result<IndexBuffer>
DawnGpuDevice::CreateIndexBuffer(const std::span<std::span<const VertexIndex>>& indices)
{
    auto nativeBufResult = CreateGpuBuffer<VertexIndex>(Device, indices);
    expect(nativeBufResult, nativeBufResult.error());

    auto [nativeBuf, sizeofBuffer] = nativeBufResult.value();

    auto ib = new DawnGpuIndexBuffer(nativeBuf);
    expect(ib, "Error allocating DawnGpuIndexBuffer");

    const uint32_t count = static_cast<uint32_t>(sizeofBuffer / sizeof(VertexIndex));

    return IndexBuffer(std::unique_ptr<DawnGpuIndexBuffer>(ib), count);
}

Result<Texture>
DawnGpuDevice::CreateTexture(const unsigned /*width*/,
    const unsigned /*height*/,
    const uint8_t* /*pixels*/,
    const unsigned /*rowStride*/,
    const imstring& /*name*/)
{
    return Result<Texture>();
}

Result<Texture>
DawnGpuDevice::CreateTexture(const RgbaColorf& /*color*/, const imstring& /*name*/)
{
    return Result<Texture>();
}

Result<void>
DawnGpuDevice::DestroyTexture(Texture& /*texture*/)
{
    return ResultOk;
}

Result<VertexShader>
DawnGpuDevice::CreateVertexShader(const VertexShaderSpec& /*shaderSpec*/)
{
    return Result<VertexShader>();
}

Result<void>
DawnGpuDevice::DestroyVertexShader(VertexShader& /*shader*/)
{
    return ResultOk;
}

Result<FragmentShader>
DawnGpuDevice::CreateFragmentShader(const FragmentShaderSpec& /*shaderSpec*/)
{
    return Result<FragmentShader>();
}

Result<void>
DawnGpuDevice::DestroyFragmentShader(FragmentShader& /*shader*/)
{
    return ResultOk;
}

Result<RenderGraph*>
DawnGpuDevice::CreateRenderGraph()
{
    return Result<RenderGraph*>();
}

void
DawnGpuDevice::DestroyRenderGraph(RenderGraph* /*renderGraph*/)
{
}

/// @brief Retrieves or creates a graphics pipeline for the given material.
Result<wgpu::RenderPipeline*>
DawnGpuDevice::GetOrCreatePipeline(const Material& /*mtl*/)
{
    return Result<wgpu::RenderPipeline*>();
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