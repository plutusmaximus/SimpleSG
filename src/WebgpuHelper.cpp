#include "WebgpuHelper.h"

#include "Color.h"
#include "Model.h"
#include "scope_exit.h"
#include "VecMath.h"

#include <SDL3/SDL.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::RGBA8Unorm;
static constexpr int kNumTextureChannels = 4;

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

    WebgpuColorPipelineLayouts ColorPipelineLayouts;
    WebgpuTransformPipelineLayouts TransformPipelineLayouts;
    WebgpuCompositorPipelineLayouts CompositorPipelineLayouts;
};
}

static void DumpDawnToggles(const wgpu::Device& device);

static Result<SDL_Window*> CreateSdlWindow(const char* appName)
{
    MLG_CHECK(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    SDL_Rect displayRect;
    MLG_CHECK(SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &displayRect), SDL_GetError());
    const int winW = displayRect.w * 3 / 4;//0.75
    const int winH = displayRect.h * 3 / 4;//0.75

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

    const bool supported =
        result->HasFeature(wgpu::FeatureName::IndirectFirstInstance);

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
        MLG_ERROR("Uncaptured error (type:{}): {}",
            static_cast<int>(errorType),
            std::string(message.data, message.length));
    };

    const char* enabledToggles[] =
    {
        //"skip_validation",
        //"disable_robustness",
        "allow_unsafe_apis",    // Required for MultiDrawIndirect
    };

    //const char* disabledToggles[] =
    //{
    //    "lazy_clear_resource_on_first_use",
    //};

    wgpu::DawnTogglesDescriptor toggles;
    toggles.enabledToggleCount = std::size(enabledToggles);
    toggles.enabledToggles = enabledToggles;
    //toggles.disabledToggleCount = std::size(disabledToggles);
    //toggles.disabledToggles = disabledToggles;

    wgpu::FeatureName requiredFeatures[] = //
        {
            wgpu::FeatureName::IndirectFirstInstance,
            //wgpu::FeatureName::MultiDrawIndirect
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

    MLG_CHECK(waitStatus == wgpu::WaitStatus::Success, "Failed to create WGPUDevice - WaitAny failed");

    MLG_CHECK(result);

    DumpDawnToggles(*result);

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
            .usage = wgpu::TextureUsage::RenderAttachment,
            .width = width,
            .height = height,
            .alphaMode = wgpu::CompositeAlphaMode::Opaque,
            .presentMode = presentMode,
        };

    surface.Configure(&config);

    return format;
}

static WgpuContext* s_WgpuContext{nullptr};
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

    s_WgpuContext = ::new (s_WgpuContextStorage) WgpuContext{
        .Window = *window,
        .Instance = std::move(*instance),
        .Adapter = std::move(*adapter),
        .Device = std::move(*device),
        .Surface = std::move(*surfaceResult),
        .SurfaceFormat = *surfaceFormat
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

Result<wgpu::Texture>
WebgpuHelper::CreateTexture(const unsigned width, const unsigned height, const imstring& name)
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

    return texture;
}

Result<wgpu::Texture>
WebgpuHelper::CreateTexture(const RgbaColorf& color, const imstring& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateTexture called before Startup");

    RgbaColoru8 colorU8{color};

    const uint8_t pixelData[4]{colorU8.r, colorU8.g, colorU8.b, colorU8.a};

    return CreateTexture(1, 1, pixelData, sizeof(pixelData), name);
}

Result<wgpu::Texture>
WebgpuHelper::CreateTexture(const unsigned width,
    const unsigned height,
    const uint8_t* pixels,
    const unsigned rowStride,
    const imstring& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateTexture called before Startup");

    auto texture = CreateTexture(width, height, name);
    MLG_CHECK(texture);

    auto stagingBuffer = CreateTextureStagingBuffer(*texture);
    MLG_CHECK(stagingBuffer);

    wgpu::Buffer buffer = stagingBuffer->GetBuffer();

    void *mapped = buffer.GetMappedRange();
    MLG_CHECK(mapped, "Failed to map staging buffer for texture upload");

    const uint8_t* srcRow = pixels;
    uint8_t* dstRow = static_cast<uint8_t*>(mapped);
    const uint32_t dstRowStride = stagingBuffer->GetRowStride();
    for(uint32_t y = 0; y < height; ++y, srcRow += rowStride, dstRow += dstRowStride)
    {
        ::memcpy(dstRow, srcRow, width * kNumTextureChannels);
    }

    buffer.Unmap();

    wgpu::CommandEncoder encoder = GetDevice().CreateCommandEncoder();

    MLG_CHECK(UploadTextureData(*texture, *stagingBuffer, encoder));

    wgpu::CommandBuffer commandBuffer = encoder.Finish();
    GetDevice().GetQueue().Submit(1, &commandBuffer);

    return texture;
}

Result<WebgpuTextureStagingBuffer>
WebgpuHelper::CreateTextureStagingBuffer(wgpu::Texture texture)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateTextureStagingBuffer called before Startup");

    const uint32_t width = texture.GetWidth();
    const uint32_t height = texture.GetHeight();

    const uint32_t rowStride = width * kNumTextureChannels;
    // Staging buffer rows must be a multiple of 256 bytes.
    const uint32_t alignedRowStride = (rowStride + 255) & ~255;
    const uint32_t stagingSize = alignedRowStride * height;

    wgpu::BufferDescriptor bufDesc = //
        {
            //.label = name.c_str(),
            .usage = wgpu::BufferUsage::CopySrc | wgpu::BufferUsage::MapWrite,
            .size = stagingSize,
            .mappedAtCreation = true,
        };

    wgpu::Buffer stagingBuffer = GetDevice().CreateBuffer(&bufDesc);

    return WebgpuTextureStagingBuffer(stagingBuffer, alignedRowStride);
}

Result<>
WebgpuHelper::UploadTextureData(
    wgpu::Texture texture, WebgpuTextureStagingBuffer stagingBuffer, wgpu::CommandEncoder encoder)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::UploadTextureData called before Startup");

    wgpu::TexelCopyBufferInfo copySrc = //
        {
            .layout = //
            {
                .offset = 0,
                .bytesPerRow = stagingBuffer.GetRowStride(),
                .rowsPerImage = texture.GetHeight(),
            },
            .buffer = stagingBuffer.GetBuffer(),
        };

    wgpu::TexelCopyTextureInfo copyDst = //
        {
            .texture = texture,
            .mipLevel = 0,
            .origin = { 0, 0, 0 },
        };

    wgpu::Extent3D copySize = //
        {
            .width = texture.GetWidth(),
            .height = texture.GetHeight(),
            .depthOrArrayLayers = 1,
        };

    encoder.CopyBufferToTexture(&copySrc, &copyDst, &copySize);

    return Result<>::Ok;
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

Result<wgpu::Buffer>
WebgpuHelper::CreateVertexBuffer(const size_t size, const imstring& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateVertexBuffer called before Startup");

    wgpu::BufferDescriptor bufferDesc //
        {
            .label = wgpu::StringView{std::string_view{name.c_str()}},
            .usage = wgpu::BufferUsage::Vertex | wgpu::BufferUsage::CopyDst,
            .size = size,
            .mappedAtCreation = false,
        };

    wgpu::Buffer buffer = GetDevice().CreateBuffer(&bufferDesc);
    return buffer;
}

Result<wgpu::Buffer>
WebgpuHelper::CreateIndexBuffer(const size_t size, const imstring& name)
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::CreateIndexBuffer called before Startup");

    wgpu::BufferDescriptor bufferDesc //
        {
            .label = wgpu::StringView{std::string_view{name.c_str()}},
            .usage = wgpu::BufferUsage::Index | wgpu::BufferUsage::CopyDst,
            .size = size,
            .mappedAtCreation = false,
        };

    wgpu::Buffer buffer = GetDevice().CreateBuffer(&bufferDesc);
    return buffer;
}

Result<WebgpuColorPipelineLayouts>
WebgpuHelper::GetColorPipelineLayouts()
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::GetColorPipelineLayouts called before Startup");

    if(!s_WgpuContext->ColorPipelineLayouts.Bindgroup0Layout)
    {
        // Color pipeline bind group 0 layout
        wgpu::BindGroupLayoutEntry entries[] =//
        {
            // World space transform.
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
            //Mesh to transform index mapping
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(TransformIndex),
                },
            },
        };
        wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "ColorPipelineBg0Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->ColorPipelineLayouts.Bindgroup0Layout =
            GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->ColorPipelineLayouts.Bindgroup0Layout,
            "Failed to create bind group 0 layout for color pipeline");
    }

    if(!s_WgpuContext->ColorPipelineLayouts.Bindgroup1Layout)
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
        };
        wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "ColorPipelineBg1Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->ColorPipelineLayouts.Bindgroup1Layout =
            GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->ColorPipelineLayouts.Bindgroup1Layout,
            "Failed to create bind group 1 layout for color pipeline");
    }

    if(!s_WgpuContext->ColorPipelineLayouts.Bindgroup2Layout)
    {
        // Color pipeline bind group 2 layout
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
            // MaterialConstants
            {
                .binding = 2,
                .visibility = wgpu::ShaderStage::Fragment,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(MaterialConstants),
                },
            }
        };

        wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "ColorPipelineBg2Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->ColorPipelineLayouts.Bindgroup2Layout =
            GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->ColorPipelineLayouts.Bindgroup2Layout,
            "Failed to create bind group 2 layout for color pipeline");
    }

    return s_WgpuContext->ColorPipelineLayouts;
}

Result<WebgpuTransformPipelineLayouts>
WebgpuHelper::GetTransformPipelineLayouts()
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::GetTransformPipelineLayouts called before Startup");

    if(!s_WgpuContext->TransformPipelineLayouts.Bindgroup0Layout)
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

        s_WgpuContext->TransformPipelineLayouts.Bindgroup0Layout =
            GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->TransformPipelineLayouts.Bindgroup0Layout,
            "Failed to create bind group 0 layout for transform pipeline");
    }

    if(!s_WgpuContext->TransformPipelineLayouts.Bindgroup1Layout)
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
        };
        wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "TransformPipelineBg1Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->TransformPipelineLayouts.Bindgroup1Layout =
            GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->TransformPipelineLayouts.Bindgroup1Layout,
            "Failed to create bind group 1 layout for transform pipeline");
    }

    if(!s_WgpuContext->TransformPipelineLayouts.Bindgroup2Layout)
    {
        // Transform pipeline bind group 2 layout
        wgpu::BindGroupLayoutEntry entries[] =//
        {
            //View/Projection matrix
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Compute,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::Uniform,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(Mat44f),
                },
            },
        };

        wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "TransformPipelineBg2Layout",
                .entryCount = std::size(entries),
                .entries = entries,
            };

        s_WgpuContext->TransformPipelineLayouts.Bindgroup2Layout =
            GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->TransformPipelineLayouts.Bindgroup2Layout,
            "Failed to create bind group 2 layout for transform pipeline");
    }

    return s_WgpuContext->TransformPipelineLayouts;
}

Result<WebgpuCompositorPipelineLayouts>
WebgpuHelper::GetCompositorPipelineLayouts()
{
    MLG_CHECKV(s_WgpuContext, "WebgpuHelper::GetCompositorPipelineLayouts called before Startup");

    if(!s_WgpuContext->CompositorPipelineLayouts.Bindgroup2Layout)
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

        s_WgpuContext->CompositorPipelineLayouts.Bindgroup2Layout =
            GetDevice().CreateBindGroupLayout(&desc);
        MLG_CHECK(s_WgpuContext->CompositorPipelineLayouts.Bindgroup2Layout,
            "Failed to create bind group 2 layout for compositor pipeline");
    }

    return s_WgpuContext->CompositorPipelineLayouts;
}

#include <dawn/native/DawnNative.h> // provides dawn::native::GetTogglesUsed
#include <iostream>

static void DumpDawnToggles(const wgpu::Device& device)
{
    auto toggles = dawn::native::GetTogglesUsed(device.Get());
    std::cout << "Device enabled toggles (" << toggles.size() << "):\n";
    for (const char* t : toggles)
    {
        std::cout << "  " << t << "\n";
    }
}

