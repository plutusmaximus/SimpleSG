#define MLG_LOGGER_NAME "WGPU"

#include "GpuHelper.h"

#include "FileFetcher.h"

#include <atomic>
#include <filesystem>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_metal.h>
#include <SDL3/SDL_video.h>
#include <string>
#include <thread>

#if !defined(EMSCRIPTEN)
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#endif

namespace
{
constexpr int kNumTextureChannels = 4;

const char*
GetBackendTypeString(const WGPUBackendType backendType)
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

const char*
GetAdapterTypeString(const WGPUAdapterType adapterType)
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

const char*
GetPresentModeString(const wgpu::PresentMode presentMode)
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
    constexpr wgpu::PresentMode modePreference[] //
        {
            wgpu::PresentMode::Mailbox,
            wgpu::PresentMode::Fifo,
            wgpu::PresentMode::Immediate,
            wgpu::PresentMode::FifoRelaxed,
        };

    for(const wgpu::PresentMode mode : modePreference)
    {
        for(const wgpu::PresentMode availableMode : availableModes)
        {
            if(mode == availableMode)
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
        if(format == wgpu::TextureFormat::BGRA8Unorm || format == wgpu::TextureFormat::RGBA8Unorm)
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
    MLG_DEBUG("Creating surface for Linux - SDL video driver: {}",
        sdlDriver ? sdlDriver : "unknown");

    const SDL_PropertiesID props = SDL_GetWindowProperties(window);

    if(sdlDriver && strcmp(sdlDriver, "wayland") == 0)
    {
        wgpu::SurfaceSourceWaylandSurface surfaceSrc{};
        surfaceSrc.display =
            SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, nullptr);
        surfaceSrc.surface =
            SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, nullptr);
        surfaceDesc.nextInChain = &surfaceSrc;
        surface = instance.CreateSurface(&surfaceDesc);
    }
    else if(sdlDriver && strcmp(sdlDriver, "x11") == 0)
    {
        wgpu::SurfaceSourceXlibWindow surfaceSrc{};
        surfaceSrc.display =
            SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
        const Sint64 windowNumber =
            SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, -1);
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
    const std::span<const wgpu::TextureFormat> formats(capabilities.formats,
        capabilities.formatCount);

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

Result<wgpu::Texture>
CreateDefaultTexture(GpuHelper& gpuHelper)
{
    constexpr size_t kDefaultTextureWidth = 128;
    constexpr size_t kDefaultTextureHeight = 128;
    constexpr RgbaColoru8 kDefaultTextureColor{ "#FF00FFFF"_rgba }; // Magenta

    auto texture =
        gpuHelper.CreateTexture(kDefaultTextureWidth, kDefaultTextureHeight, "DefaultTexture");

    MLG_CHECK(texture);

    auto stagingBuffer = gpuHelper.CreateStagingBuffer(*texture, "DefaultTextureStagingBuffer");
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
            // Magenta
            mappedSpan[offset + 0] = kDefaultTextureColor.r;
            mappedSpan[offset + 1] = kDefaultTextureColor.g;
            mappedSpan[offset + 2] = kDefaultTextureColor.b;
            mappedSpan[offset + 3] = kDefaultTextureColor.a;
        }
    }

    MLG_CHECK(gpuHelper.CommitStagingBuffer(*texture, *stagingBuffer));

    return *texture;
}

Result<wgpu::Sampler>
CreateDefaultSampler(const wgpu::Device& gpuDevice)
{
    const wgpu::SamplerDescriptor samplerDesc //
        {
            .addressModeU = wgpu::AddressMode::Repeat,
            .addressModeV = wgpu::AddressMode::Repeat,
            .addressModeW = wgpu::AddressMode::Repeat,
            .magFilter = wgpu::FilterMode::Linear,
            .minFilter = wgpu::FilterMode::Linear,
            .mipmapFilter = wgpu::MipmapFilterMode::Linear,
        };

    wgpu::Sampler sampler = gpuDevice.CreateSampler(&samplerDesc);
    MLG_CHECK(sampler, "Failed to create default sampler");

    return sampler;
}

Result<wgpu::BindGroupLayout>
CreateTextureBindGroupLayout(const wgpu::Device& gpuDevice)
{
    const wgpu::BindGroupLayoutEntry entries[]//
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
            .label = "GpuColorPass::TextureBindGroupLayout",
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    wgpu::BindGroupLayout layout = gpuDevice.CreateBindGroupLayout(&desc);
    MLG_CHECK(layout, "Failed to create texture bind group layout");

    return layout;
}

} // namespace

////////// GpuHelper::Impl

class GpuHelper::Impl
{
public:
    Impl() = default;
    ~Impl()
    {
        if(MetalView)
        {
            SDL_Metal_DestroyView(MetalView);
        }

        if(Window)
        {
            SDL_DestroyWindow(Window);
            SDL_Quit();
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;

    SDL_Window* Window{ nullptr };
    SDL_MetalView MetalView{ nullptr };
    wgpu::Instance Instance{ nullptr };
    wgpu::Adapter Adapter{ nullptr };
    wgpu::Device Device{ nullptr };
    wgpu::Surface Surface{ nullptr };
    wgpu::TextureFormat SurfaceFormat{ wgpu::TextureFormat::Undefined };
    wgpu::BindGroupLayout TextureBindGroupLayout{ nullptr };
    wgpu::Texture DefaultTexture{ nullptr };
    wgpu::Sampler DefaultSampler{ nullptr };
};

////////// GpuHelper::CreateTaskImpl

namespace
{
    struct AdapterRequestData
    {
        Result<wgpu::Adapter> Result;
        std::atomic<bool> IsComplete{false};
    };

    struct DeviceRequestData
    {
        Result<wgpu::Device> Result;
        std::atomic<bool> IsComplete{false};
    };

void
RequestAdapterCb(wgpu::RequestAdapterStatus status,
    wgpu::Adapter receivedAdapter,
    wgpu::StringView message,
    AdapterRequestData* requestData)
{
    if(status != wgpu::RequestAdapterStatus::Success)
    {
        MLG_ERROR("RequestAdapter failed: {}", std::string(message.data, message.length));
        requestData->Result = Result<>::Fail;
    }
    else
    {
        requestData->Result = std::move(receivedAdapter);
    }

    requestData->IsComplete = true;
}

void
RequestDeviceCb(wgpu::RequestDeviceStatus status,
    wgpu::Device receivedDevice,
    wgpu::StringView message,
    DeviceRequestData* requestData)
{
    if(status != wgpu::RequestDeviceStatus::Success)
    {
        MLG_ERROR("RequestDevice failed: {}", std::string(message.data, message.length));
        requestData->Result = Result<>::Fail;
    }
    else
    {
        requestData->Result = std::move(receivedDevice);
    }

    requestData->IsComplete = true;
}

// TODO(KB) - handle device lost.
void
DeviceLostCb(
    const wgpu::Device& /*device*/, wgpu::DeviceLostReason reason, wgpu::StringView message)
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
}

void
UncapturedErrorCb(
    const wgpu::Device& /*device*/, wgpu::ErrorType errorType, wgpu::StringView message)
{
    const std::string errorStr = std::format("Uncaptured error (type:{}): {}",
        static_cast<int>(errorType),
        std::string(message.data, message.length));

    MLG_ERROR(errorStr);

    MLG_ASSERT(false, errorStr);
}
} // namespace

class GpuHelper::CreateTaskImpl
{
public:
    Result<> Begin(const char* appName);

    Result<> Update();

    bool IsComplete() const
    {
        MLG_ASSERT(CreateTask::State::None != m_State, "Task is not started");
        return CreateTask::State::Succeeded == m_State || CreateTask::State::Failed == m_State;
    }

private:

    friend GpuHelper;

    Result<> CreateAdapter();

    Result<> FinalizeAdapter();

    Result<> CreateDevice();

    Result<> FinalizeDevice();

    Result<GpuHelper> Finalize();

    AdapterRequestData m_AdapterRequestData;
    DeviceRequestData m_DeviceRequestData;
    std::unique_ptr<GpuHelper::Impl> m_GpuHelperImpl;

    CreateTask::State m_State{ CreateTask::State::None };
};

Result<>
GpuHelper::CreateTaskImpl::Begin(const char* appName)
{
    MLG_ASSERT(m_State == CreateTask::State::None, "Task is already in progress");

    MLG_INFO("Creating GpuHelper...");

    std::unique_ptr<GpuHelper::Impl> gpuHelperImpl = std::make_unique<Impl>();

    auto window = CreateSdlWindow(appName);
    MLG_CHECK(window);
    gpuHelperImpl->Window = *window;

    SDL_MetalView metalView = nullptr;

#if defined(__APPLE__)
    metalView = SDL_Metal_CreateView(*window);
    MLG_CHECK(metalView, SDL_GetError());
    gpuHelperImpl->MetalView = metalView;
#endif

    auto instance = CreateInstance();
    MLG_CHECK(instance);
    gpuHelperImpl->Instance = std::move(*instance);

    auto surface = CreateSurface(*instance, *window, metalView);
    MLG_CHECK(surface);
    gpuHelperImpl->Surface = std::move(*surface);

    m_GpuHelperImpl = std::move(gpuHelperImpl);

    MLG_CHECK(CreateAdapter());

    m_State = CreateTask::State::CreatingAdapter;

    return Result<>::Ok;
}

Result<>
GpuHelper::CreateTaskImpl::Update()
{
    MLG_CHECKV(CreateTask::State::None != m_State, "Task is not started");
    MLG_CHECKV(m_GpuHelperImpl, "Task implementation is not initialized");

    m_GpuHelperImpl->Instance.ProcessEvents();

    switch(m_State)
    {
        case CreateTask::State::None:
            break;

        case CreateTask::State::CreatingAdapter:
            if(!m_AdapterRequestData.IsComplete)
            {
                break;
            }

            if(!m_AdapterRequestData.Result)
            {
                MLG_ERROR("Adapter creation failed");
                m_State = CreateTask::State::Failed;
                break;
            }

            MLG_INFO("Adapter creation succeeded");

            if(FinalizeAdapter() && CreateDevice())
            {
                m_State = CreateTask::State::CreatingDevice;
            }
            else
            {
                m_State = CreateTask::State::Failed;
            }

            break;

        case CreateTask::State::CreatingDevice:
            if(!m_DeviceRequestData.IsComplete)
            {
                break;
            }

            if(!m_DeviceRequestData.Result)
            {
                MLG_ERROR("Device creation failed");
                m_State = CreateTask::State::Failed;
                break;
            }

            MLG_INFO("Device creation succeeded");

            if(FinalizeDevice())
            {
                m_State = CreateTask::State::Succeeded;
            }
            else
            {
                m_State = CreateTask::State::Failed;
            }

            break;
        case CreateTask::State::Succeeded:
        case CreateTask::State::Failed:
            break;
    }

    MLG_CHECK(CreateTask::State::Failed != m_State, "GpuHelper creation failed");

    return Result<>::Ok;
}

// private:

Result<>
GpuHelper::CreateTaskImpl::CreateAdapter()
{
    MLG_INFO("Creating adapter...");

    EnumerateAdapters();

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
            .compatibleSurface = m_GpuHelperImpl->Surface,
        };

    m_GpuHelperImpl->Instance.RequestAdapter(&options,
        wgpu::CallbackMode::AllowSpontaneous,
        RequestAdapterCb,
        &m_AdapterRequestData);

    return Result<>::Ok;
}

Result<>
GpuHelper::CreateTaskImpl::CreateDevice()
{
    MLG_INFO("Creating device...");

    const char* const enabledToggles[] = { //"skip_validation",
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
    deviceDesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents, DeviceLostCb);
    deviceDesc.SetUncapturedErrorCallback(UncapturedErrorCb);

    m_GpuHelperImpl->Adapter.RequestDevice(&deviceDesc,
        wgpu::CallbackMode::AllowSpontaneous,
        RequestDeviceCb,
        &m_DeviceRequestData);

    return Result<>::Ok;
}

Result<>
GpuHelper::CreateTaskImpl::FinalizeAdapter()
{
    MLG_CHECK(m_AdapterRequestData.Result, "Failed to create adapter");
    m_GpuHelperImpl->Adapter = std::move(*m_AdapterRequestData.Result);

    const bool supported = m_GpuHelperImpl->Adapter.HasFeature(wgpu::FeatureName::IndirectFirstInstance);
    MLG_CHECK(supported, "IndirectFirstInstance feature is not supported");

    wgpu::AdapterInfo adapterInfo;
    m_GpuHelperImpl->Adapter.GetInfo(&adapterInfo);
    MLG_INFO("Selected adapter:");
    DumpAdapterInfo(adapterInfo);

    return Result<>::Ok;
}

Result<>
GpuHelper::CreateTaskImpl::FinalizeDevice()
{
    MLG_CHECK(m_DeviceRequestData.Result, "Failed to create device");
    m_GpuHelperImpl->Device = std::move(*m_DeviceRequestData.Result);

    DumpDawnToggles(m_GpuHelperImpl->Device);
    DumpWebgpuLimits(m_GpuHelperImpl->Device);

    return Result<>::Ok;
}

Result<GpuHelper>
GpuHelper::CreateTaskImpl::Finalize()
{
    MLG_CHECKV(IsComplete() && m_State == CreateTask::State::Succeeded,
        "CreateTask did not succeed");

    int width{ 0 }, height{ 0 };
    SDL_GetWindowSize(m_GpuHelperImpl->Window, &width, &height);

    auto surfaceFormat = ConfigureSurface(m_GpuHelperImpl->Adapter,
        m_GpuHelperImpl->Device,
        m_GpuHelperImpl->Surface,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));

    MLG_CHECK(surfaceFormat);

    m_GpuHelperImpl->SurfaceFormat = *surfaceFormat;

    auto defaultSampler = CreateDefaultSampler(m_GpuHelperImpl->Device);
    MLG_CHECK(defaultSampler);
    m_GpuHelperImpl->DefaultSampler = std::move(*defaultSampler);

    auto textureBindGroupLayout = CreateTextureBindGroupLayout(m_GpuHelperImpl->Device);
    MLG_CHECK(textureBindGroupLayout);
    m_GpuHelperImpl->TextureBindGroupLayout = std::move(*textureBindGroupLayout);

    GpuHelper gpuHelper(GpuHelper::UniquePtrType(m_GpuHelperImpl.release(), &GpuHelper::Deleter));

    auto defaultTexture = CreateDefaultTexture(gpuHelper);
    MLG_CHECK(defaultTexture);

    gpuHelper.m_Impl->DefaultTexture = std::move(*defaultTexture);

    return std::move(gpuHelper);
}

////////// GpuHelper::CreateTask

void
GpuHelper::CreateTask::Deleter(CreateTaskImpl* impl)
{
    const std::unique_ptr<CreateTaskImpl> bye(impl);
}

bool
GpuHelper::CreateTask::IsValid() const
{
    return m_TaskImpl != nullptr;
}

Result<>
GpuHelper::CreateTask::Update()
{
    MLG_CHECKV(IsValid(), "Invalid CreateTask");

    return m_TaskImpl->Update();
}

bool
GpuHelper::CreateTask::IsComplete() const
{
    return MLG_VERIFY(IsValid(), "Invalid CreateTask") && m_TaskImpl->IsComplete();
}

bool
GpuHelper::CreateTask::Succeeded() const
{
    return MLG_VERIFY(IsValid(), "Invalid CreateTask")
        && m_TaskImpl->IsComplete()
        && m_TaskImpl->m_State == State::Succeeded;
}

Result<GpuHelper>
GpuHelper::CreateTask::Get()
{
    MLG_CHECKV(Succeeded(), "CreateTask did not succeed");

    UniquePtrType bye = std::move(m_TaskImpl);

    return bye->Finalize();
}

////////// GpuHelper

Result<GpuHelper::CreateTask>
GpuHelper::Create(const char* appName)
{
    std::unique_ptr<CreateTaskImpl> createTaskImpl = std::make_unique<CreateTaskImpl>();

    MLG_CHECK(createTaskImpl->Begin(appName))

    return CreateTask(CreateTask::UniquePtrType(createTaskImpl.release(), &CreateTask::Deleter));
}

void
GpuHelper::Deleter(Impl* impl)
{
    const std::unique_ptr<Impl> bye(impl);
}

SDL_Window*
GpuHelper::GetWindow() const
{
    if(MLG_VERIFY(m_Impl, "Invalid GpuHelper"))
    {
        return m_Impl->Window;
    }
    return nullptr;
}

wgpu::Instance
GpuHelper::GetInstance() const
{
    if(MLG_VERIFY(m_Impl, "Invalid GpuHelper"))
    {
        return m_Impl->Instance;
    }
    return nullptr;
}

wgpu::Device
GpuHelper::GetDevice() const
{
    if(MLG_VERIFY(m_Impl, "Invalid GpuHelper"))
    {
        return m_Impl->Device;
    }
    return nullptr;
}

wgpu::Surface
GpuHelper::GetSurface() const
{
    if(MLG_VERIFY(m_Impl, "Invalid GpuHelper"))
    {
        return m_Impl->Surface;
    }
    return nullptr;
}

wgpu::Texture
GpuHelper::GetDefaultTexture() const
{
    if(MLG_VERIFY(m_Impl, "Invalid GpuHelper"))
    {
        return m_Impl->DefaultTexture;
    }
    return nullptr;
}

wgpu::Sampler
GpuHelper::GetDefaultSampler() const
{
    if(MLG_VERIFY(m_Impl, "Invalid GpuHelper"))
    {
        return m_Impl->DefaultSampler;
    }
    return nullptr;
}

wgpu::BindGroupLayout
GpuHelper::GetTextureBindGroupLayout() const
{
    if(MLG_VERIFY(m_Impl, "Invalid GpuHelper"))
    {
        return m_Impl->TextureBindGroupLayout;
    }
    return nullptr;
}

Dimension2
GpuHelper::GetScreenDimensions() const
{
    if(!MLG_VERIFY(m_Impl, "Invalid GpuHelper"))
    {
        return Dimension2{ .Width = 0, .Height = 0 };
    }

    int width = 0, height = 0;
    if(!SDL_GetWindowSizeInPixels(GetWindow(), &width, &height))
    {
        MLG_ERROR("Failed to get window size: {}", SDL_GetError());
    }
    return Dimension2 //
        {
            .Width = static_cast<unsigned>(width),
            .Height = static_cast<unsigned>(height),
        };
}

Result<ValidTexture>
GpuHelper::GetSwapChainTexture() const
{
    MLG_CHECKV(m_Impl, "Invalid GpuHelper");

    wgpu::SurfaceTexture surfaceTexture;
    GetSurface().GetCurrentTexture(&surfaceTexture);

    if(surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
    {
        MLG_WARN(
            "GetCurrentTexture() returned SuccessSuboptimal - the surface may need to be reconfigured");
    }

    if(surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal
        || surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal)
    {
        return ValidTexture::Create(surfaceTexture.texture);
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
    GetSurface().Unconfigure();

    auto surfaceFormat = ConfigureSurface(m_Impl->Adapter,
        GetDevice(),
        GetSurface(),
        GetScreenDimensions().Width,
        GetScreenDimensions().Height);

    MLG_CHECK(surfaceFormat);

    m_Impl->SurfaceFormat = *surfaceFormat;

    GetSurface().GetCurrentTexture(&surfaceTexture);

    MLG_CHECK(surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal
            || surfaceTexture.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessSuboptimal,
        "Failed to acquire current surface texture after reconfiguration");

    return ValidTexture::Create(surfaceTexture.texture);
}

wgpu::TextureFormat
GpuHelper::GetSwapChainFormat() const
{
    if(!MLG_VERIFY(m_Impl, "Invalid GpuHelper"))
    {
        return wgpu::TextureFormat::Undefined;
    }

    return m_Impl->SurfaceFormat;
}

Result<>
GpuHelper::Resize(const uint32_t width, const uint32_t height)
{
    MLG_CHECKV(m_Impl, "Invalid GpuHelper");

    wgpu::SurfaceTexture currentTexture;
    GetSurface().GetCurrentTexture(&currentTexture);
    if(width != currentTexture.texture.GetWidth() || height != currentTexture.texture.GetHeight())
    {
        GetSurface().Unconfigure();

        auto surfaceFormat =
            ConfigureSurface(m_Impl->Adapter, GetDevice(), GetSurface(), width, height);
        MLG_CHECK(surfaceFormat);

        m_Impl->SurfaceFormat = *surfaceFormat;
    }

    return Result<>::Ok;
}

Result<ValidShaderModule>
GpuHelper::LoadShader(const char* filePath, FileFetcher& fileFetcher) const
{
    FileFetcher::Request request(filePath);
    MLG_CHECK(fileFetcher.Fetch(request));

    while(request.IsPending())
    {
        MLG_CHECK(fileFetcher.ProcessCompletions());
        std::this_thread::yield();
    }

    MLG_CHECK(request.Succeeded(), "Failed to load shader file: {}", filePath);

    const std::string filename = std::filesystem::path(filePath).filename().string();
    const std::span<const uint8_t> data = request.GetData();

    const void* dataPtr = data.data();
    const wgpu::StringView shaderCode{ static_cast<const char*>(dataPtr), data.size() };
    const wgpu::StringView label = std::string_view(filename);
    const wgpu::ShaderSourceWGSL wgsl{ { .code = shaderCode } };
    const wgpu::ShaderModuleDescriptor desc{ .nextInChain = &wgsl, .label = label };

    const wgpu::ShaderModule shaderModule = GetDevice().CreateShaderModule(&desc);
    MLG_CHECK(shaderModule, "Failed to create shader module");

    return ValidShaderModule::Create(shaderModule);
}

Result<wgpu::Texture>
GpuHelper::CreateTexture(
    const unsigned width, const unsigned height, const std::string_view& name) const
{
    MLG_CHECKV(m_Impl, "Invalid GpuHelper");

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
    MLG_CHECK(texture, "Failed to create texture");

    return texture;
}

Result<wgpu::BindGroup>
GpuHelper::CreateTextureBindGroup(const wgpu::Texture& texture, const std::string_view& name) const
{
    MLG_CHECKV(m_Impl, "Invalid GpuHelper");

    const wgpu::BindGroupEntry entries[] = //
        {
            {
                .binding = 0,
                .textureView = texture.CreateView(),
            },
            {
                .binding = 1,
                .sampler = m_Impl->DefaultSampler,
            },
        };

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = name,
            .layout = m_Impl->TextureBindGroupLayout,
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    const wgpu::BindGroup bindGroup = GetDevice().CreateBindGroup(&desc);
    MLG_CHECKV(bindGroup, "Failed to create texture bind group");

    return bindGroup;
}

Result<ValidTexture>
GpuHelper::CreateRenderTarget(
    const unsigned width, const unsigned height, const std::string_view& name) const
{
    MLG_CHECKV(m_Impl, "Invalid GpuHelper");

    const wgpu::TextureDescriptor desc //
        {
            .label = name,
            .usage = wgpu::TextureUsage::RenderAttachment
                | wgpu::TextureUsage::TextureBinding, // For post-processing and compositing
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
    MLG_CHECK(texture, "Failed to create texture");

    return ValidTexture::Create(texture);
}

Result<ValidTexture>
GpuHelper::CreateDepthBuffer(
    const unsigned width, const unsigned height, const std::string_view& name) const
{
    const wgpu::TextureDescriptor desc //
        {
            .label = name,
            .usage = wgpu::TextureUsage::RenderAttachment,
            .dimension = wgpu::TextureDimension::e2D,
            .size = //
            {
                .width = width,
                .height = height,
                .depthOrArrayLayers = 1,
            },
            .format = kDepthBufferFormat,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };

    const wgpu::Texture depthBuffer = GetDevice().CreateTexture(&desc);
    MLG_CHECK(depthBuffer, "Failed to create depth buffer");

    return ValidTexture::Create(depthBuffer);
}

// A note on using staging buffers to upload data to the GPU:
// When copying data to textures we use a staging buffer.
// When copying data to other buffers we could also use a staging buffer, but it's simpler to use
// Queue::WriteBuffer which doesn't require a staging buffer. The equivalent of WriteBuffer for
// textures is Queue::WriteTexture howerver Queue::WriteTexture contains a bunch of validation and
// is slow compared to using a staging buffer and CopyBufferToTexture.

Result<wgpu::Buffer>
GpuHelper::CreateStagingBuffer(wgpu::Texture texture, const std::string_view& name) const
{
    MLG_CHECKV(m_Impl, "Invalid GpuHelper");

    const size_t rowStride = GetTextureAlignedRowStride(texture.GetWidth());
    const size_t sizeofBuffer = rowStride * texture.GetHeight();
    const wgpu::BufferUsage usage = wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc;

    auto buffer = CreateGpuBuffer(usage, sizeofBuffer, BufferMappedState::Mapped, name);
    MLG_CHECK(buffer, "Failed to create staging buffer");

    return buffer;
}

Result<>
GpuHelper::CommitStagingBuffer(wgpu::Texture texture, wgpu::Buffer stagingBuffer) const
{
    MLG_CHECKV(m_Impl, "Invalid GpuHelper");

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
GpuHelper::CreateVertexBuffer(const size_t count, const std::string_view& name) const
{
    const wgpu::BufferUsage usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst;

    auto buffer = CreateGpuBuffer(usage, count * sizeof(Vertex), BufferMappedState::Unmapped, name);

    MLG_CHECK(buffer, "Failed to create vertex buffer");

    return VertexBuffer::Create(GetDevice(), *buffer);
}

Result<IndexBuffer>
GpuHelper::CreateIndexBuffer(const size_t count, const std::string_view& name) const
{
    const wgpu::BufferUsage usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst;

    auto buffer =
        CreateGpuBuffer(usage, count * sizeof(VertexIndex), BufferMappedState::Unmapped, name);

    MLG_CHECK(buffer, "Failed to create index buffer");

    return IndexBuffer::Create(GetDevice(), *buffer);
}

// private:

Result<wgpu::Buffer>
GpuHelper::CreateGpuBuffer(const wgpu::BufferUsage usage,
    const size_t size,
    BufferMappedState mappedState,
    const std::string_view name) const
{
    MLG_CHECKV(m_Impl, "Invalid GpuHelper");

    const wgpu::BufferDescriptor bufferDesc //
        {
            .label = name,
            .usage = usage,
            .size = size,
            .mappedAtCreation = (mappedState == BufferMappedState::Mapped),
        };

    wgpu::Buffer buffer = GetDevice().CreateBuffer(&bufferDesc);
    MLG_CHECK(buffer, "Failed to create GPU buffer");

    return buffer;
}

Result<wgpu::Buffer>
GpuHelper::CreateIndirectBuffer(const size_t size, const std::string_view& name) const
{
    const wgpu::BufferUsage usage = wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst;

    auto buffer = CreateGpuBuffer(usage, size, BufferMappedState::Unmapped, name);
    MLG_CHECK(buffer, "Failed to create indirect buffer");

    return buffer;
}

Result<wgpu::Buffer>
GpuHelper::CreateStorageBuffer(const size_t size, const std::string_view& name) const
{
    const wgpu::BufferUsage usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst;

    auto buffer = CreateGpuBuffer(usage, size, BufferMappedState::Unmapped, name);
    MLG_CHECK(buffer, "Failed to create storage buffer");

    return buffer;
}

Result<wgpu::Buffer>
GpuHelper::CreateUniformBuffer(const size_t size, const std::string_view& name) const
{
    const wgpu::BufferUsage usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst;

    auto buffer = CreateGpuBuffer(usage, size, BufferMappedState::Unmapped, name);
    MLG_CHECK(buffer, "Failed to create uniform buffer");

    return buffer;
}

#include <dawn/native/DawnNative.h> // provides dawn::native::GetTogglesUsed

namespace
{
void
EnumerateAdapters()
{
    const dawn::native::Instance instance;

    const std::vector<dawn::native::Adapter> adapters = instance.EnumerateAdapters();

    MLG_INFO("Available adapters ({}):", adapters.size());

    size_t count = 0;
    for(const dawn::native::Adapter& a : adapters)
    {
        WGPUAdapter adapter = a.Get();

        WGPUAdapterInfo info{ .adapterType = WGPUAdapterType_Unknown };
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
    MLG_DEBUG("  Architecture: {}",
        std::string_view(adapterInfo.architecture.data, adapterInfo.architecture.length));
    MLG_DEBUG("  Device: {}", std::string_view(adapterInfo.device.data, adapterInfo.device.length));
    MLG_DEBUG("  Description: {}",
        std::string_view(adapterInfo.description.data, adapterInfo.description.length));
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
    MLG_DEBUG("  maxDynamicUniformBuffersPerPipelineLayout: {}",
        limits.maxDynamicUniformBuffersPerPipelineLayout);
    MLG_DEBUG("  maxDynamicStorageBuffersPerPipelineLayout: {}",
        limits.maxDynamicStorageBuffersPerPipelineLayout);
    MLG_DEBUG("  maxSampledTexturesPerShaderStage: {}", limits.maxSampledTexturesPerShaderStage);
    MLG_DEBUG("  maxSamplersPerShaderStage: {}", limits.maxSamplersPerShaderStage);
    MLG_DEBUG("  maxStorageBuffersPerShaderStage: {}", limits.maxStorageBuffersPerShaderStage);
    MLG_DEBUG("  maxStorageTexturesPerShaderStage: {}", limits.maxStorageTexturesPerShaderStage);
    MLG_DEBUG("  maxUniformBuffersPerShaderStage: {}", limits.maxUniformBuffersPerShaderStage);
}
} // namespace