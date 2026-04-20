#include "WebgpuHelper.h"

#include "Color.h"
#include "PropKit.h"
#include "scope_exit.h"
#include "VecMath.h"

#include <SDL3/SDL.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::RGBA8Unorm;
static constexpr int kNumTextureChannels = 4;

// Texture staging buffer rows must be a multiple of 256 bytes.
static uint32_t
GetTextureAlignedRowStride(const uint32_t textureWidth)
{
    const uint32_t rowStride = textureWidth * kNumTextureChannels;
    const uint32_t alignedRowStride = (rowStride + 255) & ~255;
    return alignedRowStride;
}

namespace
{
struct WgpuContext
{
    SDL_Window* Window;
    wgpu::Instance Instance;
    wgpu::Adapter Adapter;
    wgpu::Device Device;
    wgpu::Surface Surface;
    wgpu::TextureFormat SurfaceFormat;
    wgpu::Sampler DefaultSampler;

    std::array<wgpu::BindGroupLayout, 3> ColorPipelineLayouts;
    std::array<wgpu::BindGroupLayout, 2> TransformPipelineLayouts;
    std::array<wgpu::BindGroupLayout, 3> CompositorPipelineLayouts;
};
} // namespace

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

    return window;
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

    MLG_CHECK(instance, "Failed to create WGPUInstance");

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
            MLG_ERROR("RequestAdapter failed: {}", std::string(message.data, message.length));
            result = Result<>::Fail;
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
#if defined(_WIN32)
            .backendType = wgpu::BackendType::Vulkan,
    //.backendType = wgpu::BackendType::D3D12,
#elif defined(__EMSCRIPTEN__)
            .backendType = wgpu::BackendType::WebGPU,
#else
            .backendType = wgpu::BackendType::Vulkan,
#endif
            .compatibleSurface = nullptr,
        };

    wgpu::Future fut =
        instance.RequestAdapter(&options, wgpu::CallbackMode::WaitAnyOnly, rqstAdapterCb);

    wgpu::WaitStatus waitStatus = instance.WaitAny(fut, UINT64_MAX);

    MLG_CHECK(waitStatus == wgpu::WaitStatus::Success,
        "Failed to create WGPUAdapter - WaitAny failed");

    const bool supported = result->HasFeature(wgpu::FeatureName::IndirectFirstInstance);

    MLG_CHECK(supported, "IndirectFirstInstance feature is not supported");

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
        std::string errorStr = std::format("Uncaptured error (type:{}): {}",
            static_cast<int>(errorType),
            std::string(message.data, message.length));

        MLG_ERROR(errorStr);

        MLG_ASSERT(false, errorStr);
    };

    const char* enabledToggles[] = {
        //"skip_validation",
        //"disable_robustness",
        "allow_unsafe_apis", // Required for MultiDrawIndirect
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

    wgpu::Future fut =
        adapter.RequestDevice(&deviceDesc, wgpu::CallbackMode::WaitAnyOnly, rqstDeviceCb);

    wgpu::WaitStatus waitStatus = instance.WaitAny(fut, UINT64_MAX);

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

    wgpu::PresentMode presentMode =
        ChoosePresentMode(capabilities.presentModes, capabilities.presentModeCount);
    MLG_CHECK(presentMode != wgpu::PresentMode::Undefined, "No supported present mode found");

    wgpu::TextureFormat format =
        ChooseBackbufferFormat(capabilities.formats, capabilities.formatCount);
    MLG_CHECK(format != wgpu::TextureFormat::Undefined, "No supported backbuffer format found");

    wgpu::SurfaceConfiguration config //
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
CreateGpuBufferUnmapped(const wgpu::BufferUsage usage, const size_t size, const char* name)
{
    wgpu::BufferDescriptor bufferDesc //
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

    auto adapter = CreateAdapter(*instance);
    MLG_CHECK(adapter);

    auto device = CreateDevice(*instance, *adapter);
    MLG_CHECK(device);

    auto surfaceResult = CreateSurface(*instance, *window);
    MLG_CHECK(surfaceResult);

    int width, height;
    SDL_GetWindowSize(*window, &width, &height);

    auto surfaceFormat = ConfigureSurface(*adapter,
        *device,
        *surfaceResult,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));
    MLG_CHECK(surfaceFormat);

    s_WgpuContext = ::new(s_WgpuContextStorage) WgpuContext //
        {
            .Window = *window,
            .Instance = std::move(*instance),
            .Adapter = std::move(*adapter),
            .Device = std::move(*device),
            .Surface = std::move(*surfaceResult),
            .SurfaceFormat = *surfaceFormat,
        };

    cleanup.release();

    return Result<>::Ok;
};

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

    wgpu::TextureDescriptor desc //
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

    wgpu::Texture texture = GetDevice().CreateTexture(&desc);

    return Texture(texture);
}

Result<wgpu::Sampler>
WebgpuHelper::GetDefaultSampler()
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::GetDefaultSampler called before Startup");

    if(!s_WgpuContext->DefaultSampler)
    {
        // Create sampler
        wgpu::SamplerDescriptor samplerDesc //
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
WebgpuHelper::CreateVertexBuffer(const size_t size, const std::string& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateVertexBuffer called before Startup");

    return VertexBuffer(
        CreateGpuBufferUnmapped(wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst,
            size,
            name.c_str()));
}

Result<IndexBuffer>
WebgpuHelper::CreateIndexBuffer(const size_t size, const std::string& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateIndexBuffer called before Startup");

    return IndexBuffer(
        CreateGpuBufferUnmapped(wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst,
            size,
            name.c_str()));
}

Result<IndirectBuffer>
WebgpuHelper::CreateIndirectBuffer(const size_t size, const std::string& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateIndirectBuffer called before Startup");

    return IndirectBuffer(
        CreateGpuBufferUnmapped(wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst,
            size,
            name.c_str()));
}

Result<wgpu::Buffer>
WebgpuHelper::CreateUniformBuffer(const size_t size, const std::string& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateUniformBuffer called before Startup");

    return CreateGpuBufferUnmapped(wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
        size,
        name.c_str());
}

Result<wgpu::Buffer>
WebgpuHelper::CreateStorageBuffer(const size_t size, const std::string& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateStorageBuffer called before Startup");

    return CreateGpuBufferUnmapped(wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
            size,
            name.c_str());
}

Result<const std::array<wgpu::BindGroupLayout, 3>>
WebgpuHelper::GetColorPipelineLayouts()
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::GetColorPipelineLayouts called before Startup");

    if(!s_WgpuContext->ColorPipelineLayouts[0])
    {
        // Color pipeline bind group 0 layout
        wgpu::BindGroupLayoutEntry entries[] =//
        {
            // Mesh transform.
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderTypes::MeshTransform),
                },
            },
            // Mesh draw data.
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderTypes::MeshDrawData),
                },
            },
            // Material constants buffer.
            {
                .binding = 2,
                .visibility = wgpu::ShaderStage::Fragment,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderTypes::MaterialConstants),
                },
            },
        };
        wgpu::BindGroupLayoutDescriptor desc = //
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
        wgpu::BindGroupLayoutEntry entries[] =//
        {
            // Clip space transform
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(Mat44f),
                },
            },
            // Camera parameters
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::Uniform,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderTypes::CameraParams),
                },
            },
        };
        wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "ColorPipelineBg1Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->ColorPipelineLayouts[1] = GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->ColorPipelineLayouts[1],
            "Failed to create bind group 1 layout for color pipeline");
    }

    if(!s_WgpuContext->ColorPipelineLayouts[2])
    {
        // Color pipeline bind group 2 layout
        wgpu::BindGroupLayoutEntry entries[] =//
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

        wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "ColorPipelineBg2Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->ColorPipelineLayouts[2] = GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->ColorPipelineLayouts[2],
            "Failed to create bind group 2 layout for color pipeline");
    }

    return s_WgpuContext->ColorPipelineLayouts;
}

Result<const std::array<wgpu::BindGroupLayout, 2>>
WebgpuHelper::GetTransformPipelineLayouts()
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::GetTransformPipelineLayouts called before Startup");

    if(!s_WgpuContext->TransformPipelineLayouts[0])
    {
        // Transform pipeline bind group 0 layout
        wgpu::BindGroupLayoutEntry entries[] =//
        {
            // World space transform.
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Compute,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(Mat44f),
                },
            },
        };
        wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "TransformPipelineBg0Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->TransformPipelineLayouts[0] = GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->TransformPipelineLayouts[0],
            "Failed to create bind group 0 layout for transform pipeline");
    }

    if(!s_WgpuContext->TransformPipelineLayouts[1])
    {
        // Transform pipeline bind group 1 layout
        wgpu::BindGroupLayoutEntry entries[] =//
        {
            // Clip space transform
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Compute,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::Storage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(Mat44f),
                },
            },
            // Camera parameters
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Compute,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::Uniform,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderTypes::CameraParams),
                },
            },
        };
        wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "TransformPipelineBg1Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->TransformPipelineLayouts[1] = GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->TransformPipelineLayouts[1],
            "Failed to create bind group 1 layout for transform pipeline");
    }

    return s_WgpuContext->TransformPipelineLayouts;
}

Result<const std::array<wgpu::BindGroupLayout, 3>>
WebgpuHelper::GetCompositorPipelineLayouts()
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::GetCompositorPipelineLayouts called before Startup");

    if(!s_WgpuContext->CompositorPipelineLayouts[2])
    {
        // Compositor pipeline bind group 2 layout
        wgpu::BindGroupLayoutEntry entries[] =//
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

        wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "CompositorPipelineBg2Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->CompositorPipelineLayouts[2] = GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->CompositorPipelineLayouts[2],
            "Failed to create bind group 2 layout for compositor pipeline");
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
    return Extent{ static_cast<float>(width), static_cast<float>(height) };
}

wgpu::TextureFormat
WebgpuHelper::GetSwapChainFormat()
{
    wgpu::SurfaceTexture surfaceTexture;
    GetSurface().GetCurrentTexture(&surfaceTexture);
    MLG_ASSERT(surfaceTexture.texture, "Failed to acquire current surface texture");
    return surfaceTexture.texture.GetFormat();
}

#include <dawn/native/DawnNative.h> // provides dawn::native::GetTogglesUsed
#include <iostream>

static void
DumpDawnToggles(const wgpu::Device& device)
{
    auto toggles = dawn::native::GetTogglesUsed(device.Get());
    std::cout << "Device enabled toggles (" << toggles.size() << "):\n";
    for(const char* t : toggles)
    {
        std::cout << "  " << t << "\n";
    }
}

static void
DumpWebgpuLimits(const wgpu::Device& device)
{
    wgpu::Limits limits;
    device.GetLimits(&limits);
    std::cout << "Device limits:\n";
    std::cout << "  maxTextureDimension1D: " << limits.maxTextureDimension1D << "\n";
    std::cout << "  maxTextureDimension2D: " << limits.maxTextureDimension2D << "\n";
    std::cout << "  maxTextureDimension3D: " << limits.maxTextureDimension3D << "\n";
    std::cout << "  maxTextureArrayLayers: " << limits.maxTextureArrayLayers << "\n";
    std::cout << "  maxBindGroups: " << limits.maxBindGroups << "\n";
    std::cout << "  maxDynamicUniformBuffersPerPipelineLayout: " << limits.maxDynamicUniformBuffersPerPipelineLayout << "\n";
    std::cout << "  maxDynamicStorageBuffersPerPipelineLayout: " << limits.maxDynamicStorageBuffersPerPipelineLayout << "\n";
    std::cout << "  maxSampledTexturesPerShaderStage: " << limits.maxSampledTexturesPerShaderStage << "\n";
    std::cout << "  maxSamplersPerShaderStage: " << limits.maxSamplersPerShaderStage << "\n";
    std::cout << "  maxStorageBuffersPerShaderStage: " << limits.maxStorageBuffersPerShaderStage << "\n";
    std::cout << "  maxStorageTexturesPerShaderStage: " << limits.maxStorageTexturesPerShaderStage << "\n";
    std::cout << "  maxUniformBuffersPerShaderStage: " << limits.maxUniformBuffersPerShaderStage << "\n";
}

//////////////////////////////////////////////

Result<void*>
BasicGpuBuffer::Map()
{
    MLG_CHECKV(m_StagingBuffer == nullptr, "BasicGpuBuffer::Map called while already mapped");

    const size_t sizeofBuffer = this->GetSize();

    wgpu::Buffer stagingBuffer =
        CreateGpuBufferUnmapped(wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc,
            sizeofBuffer,
            "BasicGpuBufferStagingBuffer");

    Result<> result;

    auto cb = [](wgpu::MapAsyncStatus status, wgpu::StringView message, Result<>* result)
    {
        if(status != wgpu::MapAsyncStatus::Success)
        {
            MLG_ERROR("MapAsync failed: {}", std::string(message.data, message.length));
            *result = Result<>::Fail;
        }
        else
        {
            *result = Result<>::Ok;
        }
    };

    wgpu::Future fut = stagingBuffer.MapAsync(wgpu::MapMode::Write,
        0,
        sizeofBuffer,
        wgpu::CallbackMode::WaitAnyOnly,
        cb,
        &result);

    wgpu::WaitStatus waitStatus = WebgpuHelper::GetInstance().WaitAny(fut, UINT64_MAX);

    MLG_CHECK(waitStatus == wgpu::WaitStatus::Success,
        "Failed to map staging buffer - WaitAny failed");

    void* mapped = stagingBuffer.GetMappedRange();

    MLG_CHECK(mapped, "Failed to map staging buffer");

    m_StagingBuffer = std::move(stagingBuffer);

    return mapped;
}

Result<>
BasicGpuBuffer::Unmap()
{
    wgpu::CommandEncoder cmdEncoder = WebgpuHelper::GetDevice().CreateCommandEncoder();

    MLG_CHECK(Unmap(cmdEncoder));

    wgpu::CommandBuffer commandBuffer = cmdEncoder.Finish();

    WebgpuHelper::GetDevice().GetQueue().Submit(1, &commandBuffer);

    return Result<>::Ok;
}

Result<>
BasicGpuBuffer::Unmap(wgpu::CommandEncoder cmdEncoder)
{
    MLG_CHECKV(m_StagingBuffer, "BasicGpuBuffer::Unmap called while not mapped");

    m_StagingBuffer.Unmap();

    cmdEncoder.CopyBufferToBuffer(m_StagingBuffer, 0, *this, 0, this->GetSize());

    m_StagingBuffer = nullptr;

    return Result<>::Ok;
}

Result<void*>
Texture::Map()
{
    MLG_CHECKV(m_StagingBuffer == nullptr, "Texture::Map called while already mapped");

    // Staging buffer rows must be a multiple of 256 bytes.
    const uint32_t alignedRowStride = GetTextureAlignedRowStride(this->GetWidth());
    const uint32_t sizeofBuffer = alignedRowStride * this->GetHeight();

    wgpu::Buffer stagingBuffer =
        CreateGpuBufferUnmapped(wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc,
            sizeofBuffer,
            "TextureStagingBuffer");

    Result<> result;

    auto cb = [](wgpu::MapAsyncStatus status, wgpu::StringView message, Result<>* result)
    {
        if(status != wgpu::MapAsyncStatus::Success)
        {
            MLG_ERROR("MapAsync failed: {}", std::string(message.data, message.length));
            *result = Result<>::Fail;
        }
        else
        {
            *result = Result<>::Ok;
        }
    };

    wgpu::Future fut = stagingBuffer.MapAsync(wgpu::MapMode::Write,
        0,
        sizeofBuffer,
        wgpu::CallbackMode::WaitAnyOnly,
        cb,
        &result);

    wgpu::WaitStatus waitStatus = WebgpuHelper::GetInstance().WaitAny(fut, UINT64_MAX);

    MLG_CHECK(waitStatus == wgpu::WaitStatus::Success,
        "Failed to map staging buffer - WaitAny failed");

    void* mapped = stagingBuffer.GetMappedRange();

    MLG_CHECK(mapped, "Failed to map staging buffer");

    m_StagingBuffer = std::move(stagingBuffer);

    return mapped;
}

Result<>
Texture::Unmap()
{
    wgpu::CommandEncoder cmdEncoder = WebgpuHelper::GetDevice().CreateCommandEncoder();

    MLG_CHECK(Unmap(cmdEncoder));

    wgpu::CommandBuffer commandBuffer = cmdEncoder.Finish();

    WebgpuHelper::GetDevice().GetQueue().Submit(1, &commandBuffer);

    return Result<>::Ok;
}

Result<>
Texture::Unmap(wgpu::CommandEncoder cmdEncoder)
{
    MLG_CHECKV(m_StagingBuffer, "Texture::Unmap called while not mapped");

    m_StagingBuffer.Unmap();

    wgpu::TexelCopyBufferInfo copySrc = //
        {
            .layout = //
            {
                .offset = 0,
                .bytesPerRow = GetTextureAlignedRowStride(this->GetWidth()),
                .rowsPerImage = this->GetHeight(),
            },
            .buffer = m_StagingBuffer,
        };

    wgpu::TexelCopyTextureInfo copyDst = //
        {
            .texture = *this,
            .mipLevel = 0,
            .origin = { 0, 0, 0 },
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