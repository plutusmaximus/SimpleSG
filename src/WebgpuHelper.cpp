#include "WebgpuHelper.h"

#include "Color.h"
#include "scope_exit.h"
#include "VecMath.h"

#include <SDL3/SDL.h>

#if !defined(EMSCRIPTEN)
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#endif

static constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::RGBA8Unorm;
static constexpr int kNumTextureChannels = 4;

// Texture staging buffer rows must be a multiple of 256 bytes.
static uint32_t
GetTextureAlignedRowStride(const uint32_t textureWidth)
{
    const uint32_t rowStride = textureWidth * kNumTextureChannels;
    const uint32_t alignedRowStride = (rowStride + 255u) & ~255u;
    return alignedRowStride;
}

namespace
{
struct WgpuContext
{
    SDL_Window* Window{nullptr};
    wgpu::Instance Instance;
    wgpu::Adapter Adapter;
    wgpu::Device Device;
    wgpu::Surface Surface;
    wgpu::TextureFormat SurfaceFormat;
    wgpu::Sampler DefaultSampler;

    std::array<wgpu::BindGroupLayout, 2> ColorPipelineLayouts{};
    std::array<wgpu::BindGroupLayout, 1> TransformPipelineLayouts{};
    std::array<wgpu::BindGroupLayout, 1> CompositorPipelineLayouts{};
};
} // namespace

static Result<> DumpDawnAdapterInfo(const wgpu::Adapter& adapter);
static void DumpDawnToggles(const wgpu::Device& device);
static void DumpWebgpuLimits(const wgpu::Device& device);

static Result<SDL_Window*>
CreateSdlWindow(const char* appName)
{
    MLG_CHECK(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    SDL_Rect displayRect;
    MLG_CHECK(SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &displayRect), SDL_GetError());
    const int winW = displayRect.w * 3 / 4; // 0.75
    const int winH = displayRect.h * 3 / 4; // 0.75

    // Create window
    auto window = SDL_CreateWindow(appName, winW, winH, SDL_WINDOW_RESIZABLE);
    MLG_CHECK(window, SDL_GetError());

    SDL_ShowWindow(window);
    SDL_SyncWindow(window);

    return window;
}

static Result<wgpu::Instance>
CreateInstance()
{
    static const auto kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;
    const wgpu::InstanceDescriptor instanceDesc //
        {
            .requiredFeatureCount = 1,
            .requiredFeatures = &kTimedWaitAny,
        };
    wgpu::Instance instance = wgpu::CreateInstance(&instanceDesc);

    MLG_CHECK(instance, "Failed to create WGPUInstance");

    return instance;
}

static Result<wgpu::Adapter>
CreateAdapter(wgpu::Instance instance, wgpu::Surface surface)
{
    Result<wgpu::Adapter> result;

    auto rqstAdapterCb = [&result](wgpu::RequestAdapterStatus status,
                             wgpu::Adapter receivedAdapter,
                             wgpu::StringView message) -> void
    {
        if(status != wgpu::RequestAdapterStatus::Success)
        {
            MLG_ERROR("RequestAdapter failed: {}", std::string(message.data, message.length));
            result = Result<>::Fail;
        }
        else
        {
            result = std::move(receivedAdapter);
        }
    };

    const wgpu::RequestAdapterOptions options //
        {
            .nextInChain = nullptr,
            .powerPreference = wgpu::PowerPreference::Undefined,//wgpu::PowerPreference::HighPerformance,
            .forceFallbackAdapter = false,
#if defined(_WIN32)
            .backendType = wgpu::BackendType::Vulkan,
    //.backendType = wgpu::BackendType::D3D12,
#elif defined(__EMSCRIPTEN__)
            .backendType = wgpu::BackendType::WebGPU,
#else
            .backendType = wgpu::BackendType::Vulkan,
#endif
            .compatibleSurface = surface,
        };

    const wgpu::Future fut =
        instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly, rqstAdapterCb);

    const wgpu::WaitStatus waitStatus = instance.WaitAny(fut, UINT64_MAX);

    MLG_CHECK(waitStatus == wgpu::WaitStatus::Success,
        "Failed to create WGPUAdapter - WaitAny failed");

    MLG_CHECK(result, "Failed to create WGPUAdapter");

    const bool supported = result->HasFeature(wgpu::FeatureName::IndirectFirstInstance);

    MLG_CHECK(supported, "IndirectFirstInstance feature is not supported");

    MLG_CHECK(DumpDawnAdapterInfo(*result));

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
        MLG_ERROR("Device lost (reason:{}): {}",
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
        const std::string errorStr = std::format("Uncaptured error (type:{}): {}",
            static_cast<int>(errorType),
            std::string(message.data, message.length));

        MLG_ERROR(errorStr);

        MLG_ASSERT(false, errorStr);
    };

    const char* const enabledToggles[] = {
        //"skip_validation",
        //"disable_robustness",
        //"allow_unsafe_apis", // Required for MultiDrawIndirect
        //"backend_validation",
        ""
    };

    // const char* disabledToggles[] =
    //{
    //     "lazy_clear_resource_on_first_use",
    // };

    wgpu::DawnTogglesDescriptor toggles;
    toggles.enabledToggleCount = std::size(enabledToggles);
    toggles.enabledToggles = enabledToggles;
    // toggles.disabledToggleCount = std::size(disabledToggles);
    // toggles.disabledToggles = disabledToggles;

    wgpu::FeatureName requiredFeatures[] = //
        {
            wgpu::FeatureName::IndirectFirstInstance,
            // wgpu::FeatureName::MultiDrawIndirect
        };

    wgpu::DeviceDescriptor deviceDesc //
        {
            {
                //.nextInChain = &toggles,
                .nextInChain = nullptr,
                .label = "MainDevice",
                .requiredFeatureCount = std::size(requiredFeatures),
                .requiredFeatures = requiredFeatures,
            },
        };
    deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents, deviceLostCb);
    deviceDesc.SetUncapturedErrorCallback(uncapturedErrorCb);

    Result<wgpu::Device> result;

    auto rqstDeviceCb = [&result](wgpu::RequestDeviceStatus status,
                            wgpu::Device receivedDevice,
                            wgpu::StringView message) -> void
    {
        if(status != wgpu::RequestDeviceStatus::Success)
        {
            MLG_ERROR("RequestDevice failed: {}", std::string(message.data, message.length));
            result = Result<>::Fail;
        }
        else
        {
            result = std::move(receivedDevice);
        }
    };

    const wgpu::Future fut =
        adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly, rqstDeviceCb);

    const wgpu::WaitStatus waitStatus = instance.WaitAny(fut, UINT64_MAX);

    MLG_CHECK(waitStatus == wgpu::WaitStatus::Success,
        "Failed to create WGPUDevice - WaitAny failed");

    MLG_CHECK(result);

    DumpDawnToggles(*result);
    DumpWebgpuLimits(*result);

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

    MLG_CHECK(surface, "Failed to create WGPUSurface from SDL window");

    return surface;
}

#else

static Result<wgpu::Surface>
CreateSurface(wgpu::Instance instance, SDL_Window* window)
{
    const SDL_PropertiesID props = SDL_GetWindowProperties(window);
    wgpu::SurfaceDescriptor surfaceDesc{};
    wgpu::Surface surface;

#if defined(_WIN32)
    MLG_DEBUG("Creating surface for Win32 HWND");

    void* hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

    wgpu::SurfaceSourceWindowsHWND surfaceSrc{};
    surfaceSrc.hinstance = ::GetModuleHandle(NULL);
    surfaceSrc.hwnd = hwnd;
    surfaceDesc.nextInChain = &surfaceSrc;
    surface = instance.CreateSurface(&surfaceDesc);

#elif defined(__linux__)
    const char* sdlDriver = SDL_GetCurrentVideoDriver();
    MLG_DEBUG("Creating surface for Linux - SDL video driver: {}", sdlDriver ? sdlDriver : "unknown");

    if (sdlDriver && strcmp(sdlDriver, "wayland") == 0)
    {
        wgpu::SurfaceSourceWaylandSurface surfaceSrc{};
        surfaceSrc.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        surfaceSrc.surface = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        surfaceDesc.nextInChain = &surfaceSrc;
        surface = instance.CreateSurface(&surfaceDesc);
    }
    else if (sdlDriver && strcmp(sdlDriver, "x11") == 0)
    {
        wgpu::SurfaceSourceXlibWindow surfaceSrc{};
        surfaceSrc.display = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        const Sint64 windowNumber = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, -1);
        surfaceSrc.window = static_cast<uint64_t>(windowNumber);
        surfaceDesc.nextInChain = &surfaceSrc;
        surface = instance.CreateSurface(&surfaceDesc);
    }
    else
    {
        MLG_ERROR("Unsupported SDL video driver: {}", sdlDriver ? sdlDriver : "unknown");
    }
#else
    MLG_ERROR("Unsupported platform for surface creation");
#endif

    MLG_CHECK(surface, "Failed to create WGPUSurface from SDL window");

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
    MLG_CHECK(surface.GetCapabilities(adapter, &capabilities), "surface.GetCapabilities failed");

    const wgpu::PresentMode presentMode =
        ChoosePresentMode(capabilities.presentModes, capabilities.presentModeCount);
    MLG_CHECK(presentMode != wgpu::PresentMode::Undefined, "No supported present mode found");

    const wgpu::TextureFormat format =
        ChooseBackbufferFormat(capabilities.formats, capabilities.formatCount);
    MLG_CHECK(format != wgpu::TextureFormat::Undefined, "No supported backbuffer format found");

    const wgpu::SurfaceConfiguration config //
        {
            .device = device,
            .format = format,
            .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopyDst,
            .width = width,
            .height = height,
            .alphaMode = wgpu::CompositeAlphaMode::Opaque,
            .presentMode = presentMode,
        };

    surface.Configure(&config);

    return format;
}

static wgpu::Buffer
CreateGpuBufferUnmapped(const wgpu::BufferUsage usage, const size_t size, const std::string_view name)
{
    const wgpu::BufferDescriptor bufferDesc //
        {
            .label = name,
            .usage = usage,
            .size = size,
            .mappedAtCreation = false,
        };

    return WebgpuHelper::GetDevice().CreateBuffer(&bufferDesc);
}

static WgpuContext* s_WgpuContext{ nullptr };
static uint8_t s_WgpuContextStorage[sizeof(WgpuContext)];

Result<>
WebgpuHelper::Startup(const char* appName)
{
    MLG_CHECKV(!s_WgpuContext, "WebgpuHelper::Startup called more than once");

    auto window = CreateSdlWindow(appName);
    MLG_CHECK(window);

    auto cleanup = scope_exit{ [&window]()
        {
            SDL_DestroyWindow(*window);
            SDL_Quit();
        } };

    auto instance = CreateInstance();
    MLG_CHECK(instance);

    auto surface = CreateSurface(*instance, *window);
    MLG_CHECK(surface);

    auto adapter = CreateAdapter(*instance, *surface);
    MLG_CHECK(adapter);

    auto device = CreateDevice(*instance, *adapter);
    MLG_CHECK(device);

    int width, height;
    SDL_GetWindowSize(*window, &width, &height);

    //device->PushErrorScope(wgpu::ErrorFilter::Validation);
    auto surfaceFormat = ConfigureSurface(*adapter,
        *device,
        *surface,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));

    /*device->PopErrorScope(wgpu::CallbackMode::AllowProcessEvents,
        []([[maybe_unused]] wgpu::PopErrorScopeStatus status,
            wgpu::ErrorType errorType,
            wgpu::StringView message)
        {
            if(errorType != wgpu::ErrorType::NoError)
            {
                MLG_ERROR("Device error during surface creation (type:{}): {}",
                    static_cast<int>(errorType),
                    std::string(message.data, message.length));
            }
        });*/

    MLG_CHECK(surfaceFormat);

    s_WgpuContext = ::new(s_WgpuContextStorage) WgpuContext //
        {
            .Window = *window,
            .Instance = std::move(*instance),
            .Adapter = std::move(*adapter),
            .Device = std::move(*device),
            .Surface = std::move(*surface),
            .SurfaceFormat = *surfaceFormat,
            .DefaultSampler{},
        };

    cleanup.release();

    return Result<>::Ok;
}

void
WebgpuHelper::Shutdown()
{
    MLG_VERIFY(s_WgpuContext, "WebgpuHelper::Shutdown called before Startup");

    SDL_Window* window = s_WgpuContext->Window;

    s_WgpuContext->~WgpuContext();
    s_WgpuContext = nullptr;

    SDL_DestroyWindow(window);
    SDL_Quit();
}

SDL_Window*
WebgpuHelper::GetWindow()
{
    MLG_VERIFY(s_WgpuContext, "WebgpuHelper::GetWindow called before Startup");
    return s_WgpuContext->Window;
}

wgpu::Instance
WebgpuHelper::GetInstance()
{
    MLG_VERIFY(s_WgpuContext, "WebgpuHelper::GetInstance called before Startup");
    return s_WgpuContext->Instance;
}

wgpu::Device
WebgpuHelper::GetDevice()
{
    MLG_VERIFY(s_WgpuContext, "WebgpuHelper::GetDevice called before Startup");
    return s_WgpuContext->Device;
}

wgpu::Surface
WebgpuHelper::GetSurface()
{
    MLG_VERIFY(s_WgpuContext, "WebgpuHelper::GetSurface called before Startup");
    return s_WgpuContext->Surface;
}

Result<>
WebgpuHelper::Resize(const uint32_t width, const uint32_t height)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::Resize called before Startup");

    wgpu::SurfaceTexture currentTexture;
    s_WgpuContext->Surface.GetCurrentTexture(&currentTexture);
    if(width != currentTexture.texture.GetWidth() || height != currentTexture.texture.GetHeight())
    {
        s_WgpuContext->Surface.Unconfigure();

        auto surfaceFormat = ConfigureSurface(s_WgpuContext->Adapter,
            s_WgpuContext->Device,
            s_WgpuContext->Surface,
            width,
            height);
        MLG_CHECK(surfaceFormat);

        s_WgpuContext->SurfaceFormat = *surfaceFormat;
    }

    return Result<>::Ok;
}

Result<Texture>
WebgpuHelper::CreateTexture(const unsigned width, const unsigned height, const std::string& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateTexture called before Startup");

    const wgpu::TextureDescriptor desc //
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
            .format = kTextureFormat,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };

    const wgpu::Texture texture = GetDevice().CreateTexture(&desc);

    return Texture(texture);
}

Result<wgpu::Sampler>
WebgpuHelper::GetDefaultSampler()
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::GetDefaultSampler called before Startup");

    if(!s_WgpuContext->DefaultSampler)
    {
        // Create sampler
        const wgpu::SamplerDescriptor samplerDesc //
            {
                .label = "DefaultSampler",
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

        s_WgpuContext->DefaultSampler = GetDevice().CreateSampler(&samplerDesc);
        MLG_CHECK(s_WgpuContext->DefaultSampler, "Failed to create sampler");
    }
    return s_WgpuContext->DefaultSampler;
}

Result<VertexBuffer>
WebgpuHelper::CreateVertexBuffer(const size_t count, const std::string_view& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateVertexBuffer called before Startup");

    const wgpu::BufferUsage usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;

    return VertexBuffer(CreateGpuBufferUnmapped(usage, count * sizeof(Vertex), name));
}

Result<VertexBuffer>
WebgpuHelper::CreateVertexBuffer(std::span<const Vertex> vertices, const std::string_view& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateVertexBuffer called before Startup");

    const wgpu::BufferUsage usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;

    VertexBuffer buffer(CreateGpuBufferUnmapped(usage, vertices.size() * sizeof(Vertex), name));

    buffer.Store(0, vertices);
    return buffer;
}

Result<IndexBuffer>
WebgpuHelper::CreateIndexBuffer(const size_t count, const std::string_view& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateIndexBuffer called before Startup");

    const wgpu::BufferUsage usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;

    return IndexBuffer(CreateGpuBufferUnmapped(usage, count * sizeof(VertexIndex), name));
}

Result<IndexBuffer>
WebgpuHelper::CreateIndexBuffer(std::span<const VertexIndex> indices, const std::string_view& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateIndexBuffer called before Startup");

    const wgpu::BufferUsage usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;

    IndexBuffer buffer(CreateGpuBufferUnmapped(usage, indices.size() * sizeof(VertexIndex), name));

    buffer.Store(0, indices);
    return buffer;
}

Result<const std::array<wgpu::BindGroupLayout, 2>>
WebgpuHelper::GetColorPipelineLayouts()
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::GetColorPipelineLayouts called before Startup");

    if(!s_WgpuContext->ColorPipelineLayouts[0])
    {
        // Color pipeline bind group 0 layout
        wgpu::BindGroupLayoutEntry entries[] =//
        {
            // World transform.
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::WorldTransform),
                },
            },
            // Clip transform.
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::ClipSpaceTransform),
                },
            },
            // Mesh properties.
            {
                .binding = 2,
                .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::MeshProperties),
                },
            },
            // Material constants buffer.
            {
                .binding = 3,
                .visibility = wgpu::ShaderStage::Fragment,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::MaterialConstants),
                },
            },
            // Camera parameters
            {
                .binding = 4,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::Uniform,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::CameraParams),
                },
            },
        };
        const wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "ColorPipelineBg0Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->ColorPipelineLayouts[0] = GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->ColorPipelineLayouts[0],
            "Failed to create bind group 0 layout for color pipeline");
    }

    if(!s_WgpuContext->ColorPipelineLayouts[1])
    {
        // Color pipeline bind group 1 layout
        const wgpu::BindGroupLayoutEntry entries[] =//
        {
            // Texture
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Fragment,
                .texture =
                {
                    .sampleType = wgpu::TextureSampleType::Float,
                    .viewDimension = wgpu::TextureViewDimension::e2D,
                    .multisampled = false,
                },
            },
            // Sampler
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Fragment,
                .sampler =
                {
                    .type = wgpu::SamplerBindingType::Filtering,
                },
            },
        };

        const wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "ColorPipelineBg1Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->ColorPipelineLayouts[1] = GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->ColorPipelineLayouts[1],
            "Failed to create bind group 1 layout for color pipeline");
    }

    return s_WgpuContext->ColorPipelineLayouts;
}

Result<const std::array<wgpu::BindGroupLayout, 1>>
WebgpuHelper::GetTransformPipelineLayouts()
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::GetTransformPipelineLayouts called before Startup");

    if(!s_WgpuContext->TransformPipelineLayouts[0])
    {
        // Transform pipeline bind group 0 layout
        wgpu::BindGroupLayoutEntry entries[] =//
        {
            // World transform.
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Compute,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::WorldTransform),
                },
            },
            // Clip transform.
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Compute,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::Storage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::ClipSpaceTransform),
                },
            },
            // Camera parameters
            {
                .binding = 2,
                .visibility = wgpu::ShaderStage::Compute,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::Uniform,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::CameraParams),
                },
            },
        };
        const wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "TransformPipelineBg0Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->TransformPipelineLayouts[0] = GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->TransformPipelineLayouts[0],
            "Failed to create bind group 0 layout for transform pipeline");
    }

    return s_WgpuContext->TransformPipelineLayouts;
}

Result<const std::array<wgpu::BindGroupLayout, 1>>
WebgpuHelper::GetCompositorPipelineLayouts()
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::GetCompositorPipelineLayouts called before Startup");

    if(!s_WgpuContext->CompositorPipelineLayouts[0])
    {
        // Compositor pipeline bind group 0 layout
        const wgpu::BindGroupLayoutEntry entries[] =//
        {
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Fragment,
                .texture =
                {
                    .sampleType = wgpu::TextureSampleType::Float,
                    .viewDimension = wgpu::TextureViewDimension::e2D,
                    .multisampled = false,
                },
            },
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Fragment,
                .sampler =
                {
                    .type = wgpu::SamplerBindingType::Filtering,
                },
            },
        };

        const wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "CompositorPipelineBg0Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->CompositorPipelineLayouts[0] = GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->CompositorPipelineLayouts[0],
            "Failed to create bind group 0 layout for compositor pipeline");
    }

    return s_WgpuContext->CompositorPipelineLayouts;
}

Extent
WebgpuHelper::GetScreenBounds()
{
    int width = 0, height = 0;
    if(!SDL_GetWindowSizeInPixels(GetWindow(), &width, &height))
    {
        MLG_ERROR("Failed to get window size: {}", SDL_GetError());
    }
    return Extent{ .width = static_cast<float>(width), .height = static_cast<float>(height) };
}

wgpu::TextureFormat
WebgpuHelper::GetSwapChainFormat()
{
    wgpu::SurfaceTexture surfaceTexture;
    GetSurface().GetCurrentTexture(&surfaceTexture);
    MLG_ASSERT(surfaceTexture.texture, "Failed to acquire current surface texture");
    return surfaceTexture.texture.GetFormat();
}

// private:

Result<wgpu::Buffer>
WebgpuHelper::CreateIndirectBuffer(const size_t size, const std::string_view& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateIndirectBuffer called before Startup");

    const wgpu::BufferUsage usage = wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst;

    return CreateGpuBufferUnmapped(usage, size, name);
}

Result<wgpu::Buffer>
WebgpuHelper::CreateStorageBuffer(const size_t size, const std::string_view& name)
{
    MLG_CHECKV(nullptr != s_WgpuContext, "WebgpuHelper::CreateStorageBuffer called before Startup");

    const wgpu::BufferUsage usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;

    return CreateGpuBufferUnmapped(usage, size, name);
}

Result<wgpu::Buffer>
WebgpuHelper::CreateUniformBuffer(const size_t size, const std::string_view& name)
{
    MLG_CHECKV(nullptr != s_WgpuContext, "WebgpuHelper::CreateUniformBuffer called before Startup");

    const wgpu::BufferUsage usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    return CreateGpuBufferUnmapped(usage, size, name);
}

#include <dawn/native/DawnNative.h> // provides dawn::native::GetTogglesUsed
#include <iostream>

static Result<>
DumpDawnAdapterInfo(const wgpu::Adapter& adapter)
{
    wgpu::AdapterInfo adapterInfo;
    MLG_CHECK(adapter.GetInfo(&adapterInfo), "Failed to get adapter info");

    const char *backendTypeStr = "Unknown";
    switch(adapterInfo.backendType)
    {
        case wgpu::BackendType::D3D11:
            backendTypeStr = "D3D11";
            break;
        case wgpu::BackendType::D3D12:
            backendTypeStr = "D3D12";
            break;
        case wgpu::BackendType::Metal:
            backendTypeStr = "Metal";
            break;
        case wgpu::BackendType::Vulkan:
            backendTypeStr = "Vulkan";
            break;
        case wgpu::BackendType::WebGPU:
            backendTypeStr = "WebGPU";
            break;
        default:
            break;
    }

    const char* adapterTypeStr = "Unknown";
    switch(adapterInfo.adapterType)
    {
        case wgpu::AdapterType::DiscreteGPU:
            adapterTypeStr = "Discrete GPU";
            break;
        case wgpu::AdapterType::IntegratedGPU:
            adapterTypeStr = "Integrated GPU";
            break;
        case wgpu::AdapterType::CPU:
            adapterTypeStr = "CPU";
            break;
        default:
            break;
    }

    MLG_DEBUG("Adapter info:");
    MLG_DEBUG("  Vendor: {}", std::string_view(adapterInfo.vendor.data, adapterInfo.vendor.length));
    MLG_DEBUG("  Architecture: {}", std::string_view(adapterInfo.architecture.data, adapterInfo.architecture.length));
    MLG_DEBUG("  Device: {}", std::string_view(adapterInfo.device.data, adapterInfo.device.length));
    MLG_DEBUG("  Description: {}", std::string_view(adapterInfo.description.data, adapterInfo.description.length));
    MLG_DEBUG("  Backend Type: {}", backendTypeStr);
    MLG_DEBUG("  Adapter Type: {}", adapterTypeStr);
    MLG_DEBUG("  Vendor ID: {}", adapterInfo.vendorID);
    MLG_DEBUG("  Device ID: {}", adapterInfo.deviceID);

    return Result<>::Ok;
}

static void
DumpDawnToggles(const wgpu::Device& device)
{
    auto toggles = dawn::native::GetTogglesUsed(device.Get());
    MLG_DEBUG("Device enabled toggles ({}):", toggles.size());
    for(const char* t : toggles)
    {
        MLG_DEBUG("  {}", t);
    }
}

static void
DumpWebgpuLimits(const wgpu::Device& device)
{
    wgpu::Limits limits;
    device.GetLimits(&limits);
    MLG_DEBUG("Device limits:");
    MLG_DEBUG("  maxTextureDimension1D: {}", limits.maxTextureDimension1D);
    MLG_DEBUG("  maxTextureDimension2D: {}", limits.maxTextureDimension2D);
    MLG_DEBUG("  maxTextureDimension3D: {}", limits.maxTextureDimension3D);
    MLG_DEBUG("  maxTextureArrayLayers: {}", limits.maxTextureArrayLayers);
    MLG_DEBUG("  maxBindGroups: {}", limits.maxBindGroups);
    MLG_DEBUG("  maxDynamicUniformBuffersPerPipelineLayout: {}", limits.maxDynamicUniformBuffersPerPipelineLayout);
    MLG_DEBUG("  maxDynamicStorageBuffersPerPipelineLayout: {}", limits.maxDynamicStorageBuffersPerPipelineLayout);
    MLG_DEBUG("  maxSampledTexturesPerShaderStage: {}", limits.maxSampledTexturesPerShaderStage);
    MLG_DEBUG("  maxSamplersPerShaderStage: {}", limits.maxSamplersPerShaderStage);
    MLG_DEBUG("  maxStorageBuffersPerShaderStage: {}", limits.maxStorageBuffersPerShaderStage);
    MLG_DEBUG("  maxStorageTexturesPerShaderStage: {}", limits.maxStorageTexturesPerShaderStage);
    MLG_DEBUG("  maxUniformBuffersPerShaderStage: {}", limits.maxUniformBuffersPerShaderStage);
}

//////////////////////////////////////////////

Result<std::span<std::byte>>
Texture::MapBytes()
{
    MLG_CHECKV(m_StagingBuffer == nullptr, "Texture::MapBytes called while already mapped");

    // Staging buffer rows must be a multiple of 256 bytes.
    const uint32_t alignedRowStride = GetTextureAlignedRowStride(this->GetWidth());
    const uint32_t sizeofBuffer = alignedRowStride * this->GetHeight();
    const wgpu::BufferUsage usage = wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc;

    wgpu::Buffer stagingBuffer =
        CreateGpuBufferUnmapped(usage, sizeofBuffer, "TextureStagingBuffer");

    Result<> result;

    auto cb = [](wgpu::MapAsyncStatus status, wgpu::StringView message, Result<>* outResult)
    {
        if(status != wgpu::MapAsyncStatus::Success)
        {
            MLG_ERROR("MapAsync failed: {}", std::string(message.data, message.length));
            *outResult = Result<>::Fail;
        }
        else
        {
            *outResult = Result<>::Ok;
        }
    };

    const wgpu::Future fut = stagingBuffer.MapAsync(wgpu::MapMode::Write,
        0,
        sizeofBuffer,
        wgpu::CallbackMode::WaitAnyOnly,
        cb,
        &result);

    const wgpu::WaitStatus waitStatus = WebgpuHelper::GetInstance().WaitAny(fut, UINT64_MAX);

    MLG_CHECK(waitStatus == wgpu::WaitStatus::Success,
        "Failed to map staging buffer - WaitAny failed");

    void* mapped = stagingBuffer.GetMappedRange();

    MLG_CHECK(mapped, "Failed to map staging buffer");

    m_StagingBuffer = std::move(stagingBuffer);

    return std::span(static_cast<std::byte*>(mapped), sizeofBuffer);
}

Result<>
Texture::Unmap()
{
    const wgpu::CommandEncoder cmdEncoder = WebgpuHelper::GetDevice().CreateCommandEncoder();

    MLG_CHECK(Unmap(cmdEncoder));

    const wgpu::CommandBuffer commandBuffer = cmdEncoder.Finish();

    WebgpuHelper::GetDevice().GetQueue().Submit(1, &commandBuffer);

    return Result<>::Ok;
}

Result<>
Texture::Unmap(wgpu::CommandEncoder cmdEncoder)
{
    MLG_CHECKV(m_StagingBuffer, "Texture::Unmap called while not mapped");

    m_StagingBuffer.Unmap();

    const wgpu::TexelCopyBufferInfo copySrc = //
        {
            .layout = //
            {
                .offset = 0,
                .bytesPerRow = GetTextureAlignedRowStride(this->GetWidth()),
                .rowsPerImage = this->GetHeight(),
            },
            .buffer = m_StagingBuffer,
        };

    const wgpu::TexelCopyTextureInfo copyDst = //
        {
            .texture = *this,
            .mipLevel = 0,
            .origin{},
        };

    const wgpu::Extent3D copySize = //
        {
            .width = this->GetWidth(),
            .height = this->GetHeight(),
            .depthOrArrayLayers = 1,
        };

    cmdEncoder.CopyBufferToTexture(&copySrc, &copyDst, &copySize);

    m_StagingBuffer = nullptr;

    return Result<>::Ok;
}