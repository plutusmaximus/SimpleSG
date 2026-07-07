#define MLG_LOGGER_NAME "WGPU"

#include "GpuHelper.h"

#include "scope_exit.h"
#include "VecMath.h"

#include <array>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_metal.h>
#include <SDL3/SDL_video.h>
#include <string>

#if !defined(EMSCRIPTEN)
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#endif

namespace
{
constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::RGBA8Unorm;
constexpr int kNumTextureChannels = 4;

const char* GetBackendTypeString(const WGPUBackendType backendType)
{
    switch(backendType)
    {
        case WGPUBackendType_D3D11:
            return "D3D11";
        case WGPUBackendType_D3D12:
            return "D3D12";
        case WGPUBackendType_Metal:
            return "Metal";
        case WGPUBackendType_Vulkan:
            return "Vulkan";
        case WGPUBackendType_WebGPU:
            return "WebGPU";
        default:
            return "Unknown";
    }
}

const char* GetAdapterTypeString(const WGPUAdapterType adapterType)
{
    switch(adapterType)
    {
        case WGPUAdapterType_DiscreteGPU:
            return "Discrete GPU";
        case WGPUAdapterType_IntegratedGPU:
            return "Integrated GPU";
        case WGPUAdapterType_CPU:
            return "CPU";
        default:
            return "Unknown";
    }
}

const char* GetPresentModeString(const wgpu::PresentMode presentMode)
{
    switch(presentMode)
    {
        case wgpu::PresentMode::Undefined:
            return "Undefined";
        case wgpu::PresentMode::Fifo:
            return "Fifo";
        case wgpu::PresentMode::FifoRelaxed:
            return "FifoRelaxed";
        case wgpu::PresentMode::Mailbox:
            return "Mailbox";
        case wgpu::PresentMode::Immediate:
            return "Immediate";
        default:
            return "Unknown";
    }
}

class WgpuContext
{
public:

    SDL_Window* Window{nullptr};
    SDL_MetalView MetalView{nullptr};
    wgpu::Instance Instance{nullptr};
    wgpu::Adapter Adapter{nullptr};
    wgpu::Device Device{nullptr};
    wgpu::Surface Surface{nullptr};
    wgpu::TextureFormat SurfaceFormat{wgpu::TextureFormat::Undefined};
    wgpu::Texture DefaultTexture{nullptr};
    wgpu::Sampler DefaultSampler{nullptr};

    static inline WgpuContext* Ctx = nullptr;

    static inline auto OnShutdown = scope_exit(
        []()
        {
            MLG_ASSERT(nullptr == Ctx, "WgpuContext not properly shut down");
        });
};

alignas(WgpuContext) uint8_t g_WgpuContextBuffer[sizeof(WgpuContext)]; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

// Texture staging buffer rows must be a multiple of 256 bytes.
uint32_t
GetTextureAlignedRowStride(const uint32_t textureWidth)
{
    const uint32_t rowStride = textureWidth * kNumTextureChannels;
    const uint32_t alignedRowStride = (rowStride + 255u) & ~255u;
    return alignedRowStride;
}

void EnumerateAdapters();
void DumpAdapterInfo(const WGPUAdapterInfo& adapterInfo);
void DumpDawnToggles(const wgpu::Device& device);
void DumpWebgpuLimits(const wgpu::Device& device);

Result<SDL_Window*>
CreateSdlWindow(const char* appName)
{
    MLG_CHECK(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    const char* videoDriver = SDL_GetCurrentVideoDriver();
    MLG_INFO("SDL Video Driver: {}", videoDriver ? videoDriver : "<none>");

    int displayCount = 0;
    SDL_DisplayID* displays = SDL_GetDisplays(&displayCount);

    MLG_INFO("SDL Display Count: {}", displayCount);

    const std::span<const SDL_DisplayID> displayIdSpan(displays, static_cast<size_t>(displayCount));

    for(const SDL_DisplayID displayId : displayIdSpan)
    {
        SDL_Rect bounds{};

        if(SDL_GetDisplayBounds(displayId, &bounds))
        {
            MLG_INFO("  SDL Display id={}, bounds={}x{}+{}+{}",
                displayId,
                bounds.w,
                bounds.h,
                bounds.x,
                bounds.y);
        }
    }

    SDL_free(displays);

    SDL_Rect displayRect;
    const SDL_DisplayID primaryDisplay = SDL_GetPrimaryDisplay();
    MLG_CHECK(SDL_GetDisplayUsableBounds(primaryDisplay, &displayRect), SDL_GetError());
    const int winW = displayRect.w * 3 / 4; // 0.75
    const int winH = displayRect.h * 3 / 4; // 0.75

    SDL_WindowFlags windowFlags = SDL_WINDOW_RESIZABLE;

#if defined(__APPLE__)
    windowFlags |= SDL_WINDOW_METAL;
#endif

    SDL_Window* window = SDL_CreateWindow(appName, winW, winH, windowFlags);
    MLG_CHECK(window, SDL_GetError());

    SDL_ShowWindow(window);
    SDL_SyncWindow(window);

    return window;
}

Result<wgpu::Instance>
CreateInstance()
{
    constexpr wgpu::InstanceFeatureName kTimedWaitAny = wgpu::InstanceFeatureName::TimedWaitAny;

    const wgpu::InstanceDescriptor instanceDesc //
        {
            .requiredFeatureCount = 1,
            .requiredFeatures = &kTimedWaitAny,
        };
    wgpu::Instance instance = wgpu::CreateInstance(&instanceDesc);

    MLG_CHECK(instance, "Failed to create WGPUInstance");

    return instance;
}

Result<wgpu::Adapter>
CreateAdapter(const wgpu::Instance& instance, const wgpu::Surface& surface)
{
    EnumerateAdapters();

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
            .featureLevel = wgpu::FeatureLevel::Core,
            .powerPreference = wgpu::PowerPreference::HighPerformance,
            .forceFallbackAdapter = false,
#if defined(_WIN32)
            .backendType = wgpu::BackendType::Vulkan,
    //.backendType = wgpu::BackendType::D3D12,
#elif defined(__APPLE__)
            .backendType = wgpu::BackendType::Metal,
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

    wgpu::AdapterInfo adapterInfo;
    result->GetInfo(&adapterInfo);
    MLG_INFO("Selected adapter:");
    DumpAdapterInfo(adapterInfo);

    return result;
}

Result<wgpu::Device>
CreateDevice(const wgpu::Instance& instance, const wgpu::Adapter& adapter)
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
    toggles.enabledToggles = &enabledToggles[0];
    // toggles.disabledToggleCount = std::size(disabledToggles);
    // toggles.disabledToggles = disabledToggles;

    const wgpu::FeatureName requiredFeatures[] = //
        {
            wgpu::FeatureName::IndirectFirstInstance,
            // wgpu::FeatureName::MultiDrawIndirect
        };

    const wgpu::Limits requiredLimits{};
    /*requiredLimits.maxTextureDimension2D = 4096;
    requiredLimits.maxBindGroups = 3;
    requiredLimits.maxBindGroupsPlusVertexBuffers = 4;
    requiredLimits.maxBindingsPerBindGroup = 4;
    requiredLimits.maxStorageBuffersPerShaderStage = 3;
    requiredLimits.maxUniformBuffersPerShaderStage = 3;*/

    wgpu::DeviceDescriptor deviceDesc //
        {
            {
                //.nextInChain = &toggles,
                .nextInChain = nullptr,
                .label = "MainDevice",
                .requiredFeatureCount = std::size(requiredFeatures),
                .requiredFeatures = &requiredFeatures[0],
                .requiredLimits = &requiredLimits,
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

wgpu::PresentMode
ChoosePresentMode(const std::span<const wgpu::PresentMode> availableModes,
    const wgpu::PresentMode preferredMode)
{
    for(const wgpu::PresentMode mode : availableModes)
    {
        if(mode == preferredMode)
        {
            return mode;
        }
    }

    // Find the next best mode.
    constexpr std::array<wgpu::PresentMode, 4> modePreference //
        {
            wgpu::PresentMode::Mailbox,
            wgpu::PresentMode::Fifo,
            wgpu::PresentMode::Immediate,
            wgpu::PresentMode::FifoRelaxed,
        };

    for (const wgpu::PresentMode mode : modePreference)
    {
        for (const wgpu::PresentMode availableMode : availableModes)
        {
            if (mode == availableMode)
            {
                return mode;
            }
        }
    }

    return wgpu::PresentMode::Undefined;
}

wgpu::TextureFormat
ChooseBackbufferFormat(const std::span<const wgpu::TextureFormat> availableFormats)
{
    // Prefer BGRA8Unorm if available
    for(const wgpu::TextureFormat format : availableFormats)
    {
        if(format == wgpu::TextureFormat::BGRA8Unorm ||
            format == wgpu::TextureFormat::RGBA8Unorm)
        {
            return format;
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

Result<wgpu::Surface>
CreateSurface(const wgpu::Instance& instance,
    [[maybe_unused]] SDL_Window* window,
    [[maybe_unused]] SDL_MetalView metalView)
{
    wgpu::SurfaceDescriptor surfaceDesc{};
    wgpu::Surface surface;

#if defined(_WIN32)

    MLG_DEBUG("Creating surface for Win32 HWND");

    const SDL_PropertiesID props = SDL_GetWindowProperties(window);

    void* hwnd = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);

    wgpu::SurfaceSourceWindowsHWND surfaceSrc{};
    surfaceSrc.hinstance = ::GetModuleHandle(NULL);
    surfaceSrc.hwnd = hwnd;
    surfaceDesc.nextInChain = &surfaceSrc;
    surface = instance.CreateSurface(&surfaceDesc);

#elif defined(__linux__)

    const char* sdlDriver = SDL_GetCurrentVideoDriver();
    MLG_DEBUG("Creating surface for Linux - SDL video driver: {}", sdlDriver ? sdlDriver : "unknown");

    const SDL_PropertiesID props = SDL_GetWindowProperties(window);

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
#elif defined(__APPLE__)

    MLG_DEBUG("Creating surface for Metal");

    void* metalLayer = SDL_Metal_GetLayer(metalView);
    MLG_CHECK(metalLayer, SDL_GetError());

    wgpu::SurfaceSourceMetalLayer surfaceSrc{};
    surfaceSrc.layer = metalLayer;
    surfaceDesc.nextInChain = &surfaceSrc;
    surface = instance.CreateSurface(&surfaceDesc);
#else
    MLG_ERROR("Unsupported platform for surface creation");
#endif

    MLG_CHECK(surface, "Failed to create WGPUSurface from SDL window");

    return surface;
}

#endif // defined(__EMSCRIPTEN__)

Result<wgpu::TextureFormat>
ConfigureSurface(const wgpu::Adapter& adapter,
    const wgpu::Device& device,
    const wgpu::Surface& surface,
    const uint32_t width,
    const uint32_t height)
{
    wgpu::SurfaceCapabilities capabilities;
    MLG_CHECK(surface.GetCapabilities(adapter, &capabilities), "surface.GetCapabilities failed");

    const std::span<const wgpu::PresentMode> presentModes(capabilities.presentModes,
        capabilities.presentModeCount);
    const std::span<const wgpu::TextureFormat> formats(capabilities.formats, capabilities.formatCount);

    const wgpu::PresentMode presentMode =
        ChoosePresentMode(presentModes, wgpu::PresentMode::Immediate);
    MLG_CHECK(presentMode != wgpu::PresentMode::Undefined, "No supported present mode found");

    MLG_INFO("Present mode: {}", GetPresentModeString(presentMode));

    const wgpu::TextureFormat format = ChooseBackbufferFormat(formats);
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

} // namespace

Result<>
GpuHelper::Startup(const char* appName)
{
    MLG_CHECKV(!WgpuContext::Ctx, "GpuHelper::Startup called more than once");

    MLG_INFO("Starting WebGPU...");

    auto window = CreateSdlWindow(appName);
    MLG_CHECK(window);

    MLG_DEFER_AS(cleanupWindow)
    {
        SDL_DestroyWindow(*window);
        SDL_Quit();
    };

    SDL_MetalView metalView = nullptr;
    
#if defined(__APPLE__)
     metalView = SDL_Metal_CreateView(*window);
    MLG_CHECK(metalView, SDL_GetError());
#endif

    MLG_DEFER_AS(cleanupMetalView)
    {
        if(metalView)
        {
            SDL_Metal_DestroyView(metalView);
        }
    };

    auto instance = CreateInstance();
    MLG_CHECK(instance);

    auto surface = CreateSurface(*instance, *window, metalView);
    MLG_CHECK(surface);

    auto adapter = CreateAdapter(*instance, *surface);
    MLG_CHECK(adapter);

    auto device = CreateDevice(*instance, *adapter);
    MLG_CHECK(device);

    int width{0}, height{0};
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

    WgpuContext context //
        {
            .Window = *window,
            .MetalView = metalView,
            .Instance = std::move(*instance),
            .Adapter = std::move(*adapter),
            .Device = std::move(*device),
            .Surface = std::move(*surface),
            .SurfaceFormat = *surfaceFormat
        };

    // Create the context so we can call CreateDefaultTexture() and CreateDefaultSampler() below.
    void* contextPtr = static_cast<void*>(g_WgpuContextBuffer);
    WgpuContext* contextMem = static_cast<WgpuContext*>(contextPtr);
    WgpuContext::Ctx = std::construct_at(contextMem, std::move(context));

    cleanupMetalView.release();
    cleanupWindow.release();

    MLG_DEFER_AS(cleanup)
    {
        Shutdown();
    };

    auto defaultTexture = CreateDefaultTexture();
    MLG_CHECK(defaultTexture);

    auto defaultSampler = CreateDefaultSampler();
    MLG_CHECK(defaultSampler);

    WgpuContext::Ctx->DefaultTexture = std::move(*defaultTexture);
    WgpuContext::Ctx->DefaultSampler = std::move(*defaultSampler);

    cleanup.release();

    return Result<>::Ok;
}

void
GpuHelper::Shutdown()
{
    MLG_VERIFY(WgpuContext::Ctx, "GpuHelper::Shutdown called before Startup");

    SDL_Window* window = WgpuContext::Ctx->Window;
    SDL_MetalView metalView = WgpuContext::Ctx->MetalView;

    std::destroy_at(WgpuContext::Ctx);
    WgpuContext::Ctx = nullptr;

    if(metalView)
    {
        SDL_Metal_DestroyView(metalView);
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
}

SDL_Window*
GpuHelper::GetWindow()
{
    MLG_VERIFY(WgpuContext::Ctx, "GpuHelper::GetWindow called before Startup");
    return WgpuContext::Ctx->Window;
}

wgpu::Instance
GpuHelper::GetInstance()
{
    MLG_VERIFY(WgpuContext::Ctx, "GpuHelper::GetInstance called before Startup");
    return WgpuContext::Ctx->Instance;
}

wgpu::Device
GpuHelper::GetDevice()
{
    MLG_VERIFY(WgpuContext::Ctx, "GpuHelper::GetDevice called before Startup");
    return WgpuContext::Ctx->Device;
}

wgpu::Surface
GpuHelper::GetSurface()
{
    MLG_VERIFY(WgpuContext::Ctx, "GpuHelper::GetSurface called before Startup");
    return WgpuContext::Ctx->Surface;
}

wgpu::Texture
GpuHelper::GetDefaultTexture()
{
    MLG_VERIFY(WgpuContext::Ctx, "GpuHelper::GetDefaultTexture called before Startup");
    return WgpuContext::Ctx->DefaultTexture;
}

wgpu::Sampler
GpuHelper::GetDefaultSampler()
{
    MLG_VERIFY(WgpuContext::Ctx, "GpuHelper::GetDefaultSampler called before Startup");
    return WgpuContext::Ctx->DefaultSampler;
}

Extent
GpuHelper::GetScreenBounds()
{
    int width = 0, height = 0;
    if(!SDL_GetWindowSizeInPixels(GetWindow(), &width, &height))
    {
        MLG_ERROR("Failed to get window size: {}", SDL_GetError());
    }
    return Extent //
        {
            .Width = static_cast<unsigned>(width),
            .Height = static_cast<unsigned>(height),
        };
}

Result<wgpu::Texture>
GpuHelper::GetSwapChainTexture()
{
    wgpu::SurfaceTexture surfaceTexture;
    GetSurface().GetCurrentTexture(&surfaceTexture);

    if(surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
    {
        MLG_WARN(
            "GetCurrentTexture() returned SuccessSuboptimal - the surface may need to be reconfigured");
    }

    if(surfaceTexture.status
        == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal
        || surfaceTexture.status
        == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
    {
        return surfaceTexture.texture;
    }

    MLG_CHECK(surfaceTexture.status != wgpu::SurfaceGetCurrentTextureStatus::Error,
        "Failed to acquire current surface texture: Unknown error");

    if(surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::Timeout)
    {
        MLG_WARN("Failed to acquire current surface texture: Timeout");
    }
    else if(surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::Outdated)
    {
        MLG_WARN("Failed to acquire current surface texture: Outdated");
    }
    else if(surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::Lost)
    {
        MLG_WARN("Failed to acquire current surface texture: Lost");
    }

    // Attempt to reconfigure the surface and acquire the texture again
    WgpuContext::Ctx->Surface.Unconfigure();

    auto surfaceFormat = ConfigureSurface(WgpuContext::Ctx->Adapter,
        WgpuContext::Ctx->Device,
        WgpuContext::Ctx->Surface,
        GetScreenBounds().Width,
        GetScreenBounds().Height);

    MLG_CHECK(surfaceFormat);

    WgpuContext::Ctx->SurfaceFormat = *surfaceFormat;

    GetSurface().GetCurrentTexture(&surfaceTexture);

    MLG_CHECK(surfaceTexture.status
            == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal
            || surfaceTexture.status
            == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal,
        "Failed to acquire current surface texture after reconfiguration");

    return surfaceTexture.texture;
}

wgpu::TextureFormat
GpuHelper::GetSwapChainFormat()
{
    if(!MLG_VERIFY(WgpuContext::Ctx, "GpuHelper::GetSwapChainFormat called before Startup"))
    {
        return wgpu::TextureFormat::Undefined;
    }

    return WgpuContext::Ctx->SurfaceFormat;
}

Result<>
GpuHelper::Resize(const uint32_t width, const uint32_t height)
{
    MLG_CHECKV(WgpuContext::Ctx, "GpuHelper::Resize called before Startup");

    wgpu::SurfaceTexture currentTexture;
    WgpuContext::Ctx->Surface.GetCurrentTexture(&currentTexture);
    if(width != currentTexture.texture.GetWidth() || height != currentTexture.texture.GetHeight())
    {
        WgpuContext::Ctx->Surface.Unconfigure();

        auto surfaceFormat = ConfigureSurface(WgpuContext::Ctx->Adapter,
            WgpuContext::Ctx->Device,
            WgpuContext::Ctx->Surface,
            width,
            height);
        MLG_CHECK(surfaceFormat);

        WgpuContext::Ctx->SurfaceFormat = *surfaceFormat;
    }

    return Result<>::Ok;
}

Result<wgpu::Texture>
GpuHelper::CreateTexture(const unsigned width, const unsigned height, const std::string_view& name)
{
    MLG_CHECKV(WgpuContext::Ctx, "GpuHelper::CreateTexture called before Startup");

    const wgpu::TextureDescriptor desc //
        {
            .label = name,
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

    wgpu::Texture texture = GetDevice().CreateTexture(&desc);

    return texture;
}

// A note on using staging buffers to upload data to the GPU:
// When copying data to textures we use a staging buffer.
// When copying data to other buffers we could also use a staging buffer, but it's simpler to use
// Queue::WriteBuffer which doesn't require a staging buffer. The equivalent of WriteBuffer for
// textures is Queue::WriteTexture howerver Queue::WriteTexture contains a bunch of validation and
// is slow compared to using a staging buffer and CopyBufferToTexture.

Result<wgpu::Buffer>
GpuHelper::CreateStagingBuffer(wgpu::Texture texture, const std::string_view& name)
{
    MLG_CHECKV(WgpuContext::Ctx, "GpuHelper::CreateStagingBuffer called before Startup");

    const size_t rowStride = GetTextureAlignedRowStride(texture.GetWidth());
    const size_t sizeofBuffer = rowStride * texture.GetHeight();
    const wgpu::BufferUsage usage = wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc;

    auto stagingBuffer =
        CreateGpuBuffer(usage, sizeofBuffer, BufferMappedState::Mapped, name);

    return stagingBuffer;
}

Result<>
GpuHelper::CommitStagingBuffer(wgpu::Texture texture, wgpu::Buffer stagingBuffer)
{
    MLG_CHECKV(WgpuContext::Ctx, "GpuHelper::CommitStagingBuffer called before Startup");

    const wgpu::CommandEncoder cmdEncoder = GetDevice().CreateCommandEncoder();

    MLG_CHECK(CommitStagingBuffer(texture, stagingBuffer, cmdEncoder));

    const wgpu::CommandBuffer commandBuffer = cmdEncoder.Finish();

    GetDevice().GetQueue().Submit(1, &commandBuffer);

    return Result<>::Ok;
}

Result<>
GpuHelper::CommitStagingBuffer(
    wgpu::Texture texture, wgpu::Buffer stagingBuffer, wgpu::CommandEncoder cmdEncoder)
{
    stagingBuffer.Unmap();

    const wgpu::TexelCopyBufferInfo copySrc = //
        {
            .layout = //
            {
                .offset = 0,
                .bytesPerRow = GetTextureAlignedRowStride(texture.GetWidth()),
                .rowsPerImage = texture.GetHeight(),
            },
            .buffer = stagingBuffer,
        };

    const wgpu::TexelCopyTextureInfo copyDst = //
        {
            .texture = texture,
            .mipLevel = 0,
            .origin{},
        };

    const wgpu::Extent3D copySize = //
        {
            .width = texture.GetWidth(),
            .height = texture.GetHeight(),
            .depthOrArrayLayers = 1,
        };

    cmdEncoder.CopyBufferToTexture(&copySrc, &copyDst, &copySize);

    return Result<>::Ok;
}

Result<VertexBuffer>
GpuHelper::CreateVertexBuffer(const size_t count, const std::string_view& name)
{
    MLG_CHECKV(WgpuContext::Ctx, "GpuHelper::CreateVertexBuffer called before Startup");

    const wgpu::BufferUsage usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;

    auto buffer = CreateGpuBuffer(usage, count * sizeof(Vertex), BufferMappedState::Unmapped, name);

    MLG_CHECK(buffer, "Failed to create vertex buffer");

    return VertexBuffer(GetDevice(), *buffer);
}

Result<IndexBuffer>
GpuHelper::CreateIndexBuffer(const size_t count, const std::string_view& name)
{
    MLG_CHECKV(WgpuContext::Ctx, "GpuHelper::CreateIndexBuffer called before Startup");

    const wgpu::BufferUsage usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;

    auto buffer = CreateGpuBuffer(usage, count * sizeof(VertexIndex), BufferMappedState::Unmapped, name);

    MLG_CHECK(buffer, "Failed to create index buffer");

    return IndexBuffer(GetDevice(), *buffer);
}

// private:

Result<wgpu::Buffer>
GpuHelper::CreateGpuBuffer(const wgpu::BufferUsage usage,
    const size_t size,
    BufferMappedState mappedState,
    const std::string_view name)
{
    MLG_CHECKV(WgpuContext::Ctx, "GpuHelper::CreateGpuBuffer called before Startup");

    const wgpu::BufferDescriptor bufferDesc //
        {
            .label = name,
            .usage = usage,
            .size = size,
            .mappedAtCreation = (mappedState == BufferMappedState::Mapped),
        };

    return GetDevice().CreateBuffer(&bufferDesc);
}

Result<wgpu::Buffer>
GpuHelper::CreateIndirectBuffer(const size_t size, const std::string_view& name)
{
    const wgpu::BufferUsage usage = wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst;

    return CreateGpuBuffer(usage, size, BufferMappedState::Unmapped, name);
}

Result<wgpu::Buffer>
GpuHelper::CreateStorageBuffer(const size_t size, const std::string_view& name)
{
    const wgpu::BufferUsage usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;

    return CreateGpuBuffer(usage, size, BufferMappedState::Unmapped, name);
}

Result<wgpu::Buffer>
GpuHelper::CreateUniformBuffer(const size_t size, const std::string_view& name)
{
    const wgpu::BufferUsage usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    return CreateGpuBuffer(usage, size, BufferMappedState::Unmapped, name);
}

Result<wgpu::Texture>
GpuHelper::CreateDefaultTexture()
{
    constexpr size_t kDefaultTextureWidth = 128;
    constexpr size_t kDefaultTextureHeight = 128;
    constexpr RgbaColoru8 kDefaultTextureColor{ "#FF00FFFF"_rgba }; // Magenta

    auto texture = CreateTexture(
        kDefaultTextureWidth,
        kDefaultTextureHeight,
        "DefaultTexture");

    MLG_CHECK(texture);

    auto stagingBuffer = CreateStagingBuffer(*texture, "DefaultTextureStagingBuffer");
    MLG_CHECK(stagingBuffer);
    
    void* mapped = stagingBuffer->GetMappedRange();
    MLG_CHECK(mapped);

    const std::span<uint8_t> mappedSpan(static_cast<uint8_t*>(mapped), stagingBuffer->GetSize());

    const size_t rowStride = GetTextureAlignedRowStride(kDefaultTextureWidth);

    for(size_t y = 0; y < kDefaultTextureHeight; ++y)
    {
        size_t offset = y * rowStride;

        for(size_t x = 0; x < kDefaultTextureWidth; ++x, offset += 4)
        {
            //Magenta
            mappedSpan[offset + 0] = kDefaultTextureColor.r;
            mappedSpan[offset + 1] = kDefaultTextureColor.g;
            mappedSpan[offset + 2] = kDefaultTextureColor.b;
            mappedSpan[offset + 3] = kDefaultTextureColor.a;
        }
    }

    MLG_CHECK(CommitStagingBuffer(*texture, *stagingBuffer));

    return *texture;
}

Result<wgpu::Sampler>
GpuHelper::CreateDefaultSampler()
{
    MLG_CHECKV(WgpuContext::Ctx, "GpuHelper::CreateDefaultSampler called before Startup");

    const wgpu::SamplerDescriptor samplerDesc//
    {
        .addressModeU = wgpu::AddressMode::Repeat,
        .addressModeV = wgpu::AddressMode::Repeat,
        .addressModeW = wgpu::AddressMode::Repeat,
        .magFilter = wgpu::FilterMode::Linear,
        .minFilter = wgpu::FilterMode::Linear,
        .mipmapFilter = wgpu::MipmapFilterMode::Linear,
    };

    return GetDevice().CreateSampler(&samplerDesc);
}

#include <dawn/native/DawnNative.h> // provides dawn::native::GetTogglesUsed

namespace
{
void EnumerateAdapters()
{
    const dawn::native::Instance instance;

    const std::vector<dawn::native::Adapter> adapters = instance.EnumerateAdapters();

    MLG_INFO("Available adapters ({}):", adapters.size());

    size_t count = 0;
    for (const dawn::native::Adapter& a : adapters)
    {
        WGPUAdapter adapter = a.Get();

        WGPUAdapterInfo info{.adapterType = WGPUAdapterType_Unknown};
        wgpuAdapterGetInfo(adapter, &info);

        MLG_INFO("Adapter {}:", count++);
        DumpAdapterInfo(info);

        wgpuAdapterInfoFreeMembers(info);
    }
}

void
DumpAdapterInfo(const WGPUAdapterInfo& adapterInfo)
{
    MLG_DEBUG("  Vendor: {}", std::string_view(adapterInfo.vendor.data, adapterInfo.vendor.length));
    MLG_DEBUG("  Architecture: {}", std::string_view(adapterInfo.architecture.data, adapterInfo.architecture.length));
    MLG_DEBUG("  Device: {}", std::string_view(adapterInfo.device.data, adapterInfo.device.length));
    MLG_DEBUG("  Description: {}", std::string_view(adapterInfo.description.data, adapterInfo.description.length));
    MLG_DEBUG("  Backend Type: {}", GetBackendTypeString(adapterInfo.backendType));
    MLG_DEBUG("  Adapter Type: {}", GetAdapterTypeString(adapterInfo.adapterType));
    MLG_DEBUG("  Vendor ID: {}", adapterInfo.vendorID);
    MLG_DEBUG("  Device ID: {}", adapterInfo.deviceID);
}

void
DumpDawnToggles(const wgpu::Device& device)
{
    auto toggles = dawn::native::GetTogglesUsed(device.Get());
    MLG_DEBUG("Device enabled toggles ({}):", toggles.size());
    for(const char* t : toggles)
    {
        MLG_DEBUG("  {}", t);
    }
}

void
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
} // namespace