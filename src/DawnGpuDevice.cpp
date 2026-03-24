#include "DawnGpuDevice.h"

#include "Log.h"
#include "Material.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu_cpp.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static constexpr wgpu::TextureFormat kTextureFormat = wgpu::TextureFormat::RGBA8Unorm;

/// @brief Traits to map a CPU-side buffer type to its corresponding GPU buffer type and usage flags.
template<typename T> struct GpuBufferTraits;

template<> struct GpuBufferTraits<VertexIndex>
{
    static constexpr wgpu::BufferUsage Usage = wgpu::BufferUsage::Index;
    static constexpr const char* DebugName = "IndexBuffer";
};

template<> struct GpuBufferTraits<Vertex>
{
    static constexpr wgpu::BufferUsage Usage = wgpu::BufferUsage::Vertex;
    static constexpr const char* DebugName = "VertexBuffer";
};

// Helper function to create a GPU buffer from multiple spans of data
template<typename T>
static Result<std::tuple<wgpu::Buffer, size_t>>
CreateGpuBuffer(wgpu::Device device, const std::span<const std::span<const T>>& spans);

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

static void DumpDawnToggles(const wgpu::Device& device);

/// @brief Aligns the size of a uniform buffer to the GPU's minimum alignment requirements.
template<typename T>
static inline size_t alignUniformBuffer(const wgpu::Limits& limits)
{
    const size_t alignment = limits.minUniformBufferOffsetAlignment;

    return (sizeof(T) + alignment - 1) & ~(alignment - 1);
}

Result<>
DawnGpuStorageBuffer::WriteBuffer(const std::span<const uint8_t>& data)
{
    m_GpuDevice->Device.GetQueue().WriteBuffer(m_Buffer, 0, data.data(), data.size());

    return Result<>::Ok;
}

Result<>
DawnGpuDrawIndirectBuffer::WriteBuffer(const std::span<const uint8_t>& data)
{
    m_GpuDevice->Device.GetQueue().WriteBuffer(m_Buffer, 0, data.data(), data.size());

    return Result<>::Ok;
}

DawnGpuDevice::DawnGpuDevice(SDL_Window* window,
    wgpu::Instance instance,
    wgpu::Adapter adapter,
    wgpu::Device device,
    wgpu::Surface surface,
    const wgpu::TextureFormat surfaceFormat)
    : Window(window),
      Instance(instance),
      Adapter(adapter),
      Device(device),
      Surface(surface),
      m_SwapChainFormat(surfaceFormat)
{
    auto rendererResult = DawnRenderer::Create(this);
    m_Renderer = *rendererResult;
    m_RenderCompositor = ::new(m_RenderCompositorBuffer)DawnRenderCompositor(this);
}

Result<GpuDevice*>
DawnGpuDevice::Create(SDL_Window* window)
{
    MLG_INFO("Creating Dawn GPU Device...");

    auto instanceResult = CreateInstance();
    MLG_CHECK(instanceResult);

    auto adapterResult = CreateAdapter(*instanceResult);
    MLG_CHECK(adapterResult);

    auto deviceResult = CreateDevice(*instanceResult, *adapterResult);
    MLG_CHECK(deviceResult);

    auto surfaceResult = CreateSurface(*instanceResult, window);
    MLG_CHECK(surfaceResult);

    int width, height;
    SDL_GetWindowSize(window, &width, &height);

    auto configureSurfaceResult = ConfigureSurface(*adapterResult,
        *deviceResult,
        *surfaceResult,
        static_cast<uint32_t>(width),
        static_cast<uint32_t>(height));
    MLG_CHECK(configureSurfaceResult);

    wgpu::TextureFormat surfaceFormat = *configureSurfaceResult;

    DawnGpuDevice* dawnDevice = new DawnGpuDevice(window, *instanceResult, *adapterResult, *deviceResult, *surfaceResult, surfaceFormat);
    MLG_CHECKV(dawnDevice, "Error allocating device");

    return dawnDevice;
}

void
DawnGpuDevice::Destroy(GpuDevice* device)
{
    delete static_cast<DawnGpuDevice*>(device);
}

DawnGpuDevice::~DawnGpuDevice()
{
    m_RenderCompositor->~DawnRenderCompositor();

    DawnRenderer::Destroy(m_Renderer);

    m_Renderer = nullptr;
    m_RenderCompositor = nullptr;

    // Nothing else to do here. The wgpu::Device, wgpu::Adapter, wgpu::Instance, and wgpu::Surface will
    // be automatically released when their destructors are called.
}

Extent
DawnGpuDevice::GetScreenBounds() const
{
    int width = 0, height = 0;
    if(!SDL_GetWindowSizeInPixels(Window, &width, &height))
    {
        MLG_ERROR("Failed to get window size: {}", SDL_GetError());
    }
    return Extent{static_cast<float>(width), static_cast<float>(height)};
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
    MLG_CHECK(nativeBufResult);

    auto [nativeBuf, sizeofBuffer] = *nativeBufResult;

    GpuResource* res = m_ResourceAllocator.New();

    MLG_CHECKV(res, "Error allocating DawnGpuVertexBuffer");

    return ::new(&res->VertexBuffer)
        DawnGpuVertexBuffer(this, nativeBuf);
}

Result<>
DawnGpuDevice::DestroyVertexBuffer(GpuVertexBuffer* vb)
{
    DawnGpuVertexBuffer* dawnBuffer = static_cast<DawnGpuVertexBuffer*>(vb);
    MLG_ASSERT(this == dawnBuffer->m_GpuDevice, "VertexBuffer does not belong to this device");
    dawnBuffer->m_GpuDevice = nullptr;
    dawnBuffer->~DawnGpuVertexBuffer();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(vb));
    return Result<>::Ok;
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
    MLG_CHECK(nativeBufResult);

    auto [nativeBuf, sizeofBuffer] = *nativeBufResult;

    GpuResource* res = m_ResourceAllocator.New();

    MLG_CHECKV(res, "Error allocating DawnGpuIndexBuffer");

    return ::new(&res->IndexBuffer)
        DawnGpuIndexBuffer(this, nativeBuf);
}

Result<>
DawnGpuDevice::DestroyIndexBuffer(GpuIndexBuffer* buffer)
{
    DawnGpuIndexBuffer* dawnBuffer = static_cast<DawnGpuIndexBuffer*>(buffer);
    MLG_ASSERT(this == dawnBuffer->m_GpuDevice, "IndexBuffer does not belong to this device");
    dawnBuffer->m_GpuDevice = nullptr;
    dawnBuffer->~DawnGpuIndexBuffer();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(buffer));
    return Result<>::Ok;
}

Result<GpuStorageBuffer*>
DawnGpuDevice::CreateStorageBuffer(const size_t size)
{
    // Size must be a multiple of 4
    const size_t bufferSize = (size + 3) & ~0x03;

    wgpu::BufferDescriptor bufferDesc //
        {
            .label = "ReadonlyBuffer",
            .usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
            .size = bufferSize,
            .mappedAtCreation = false,
        };

    wgpu::Buffer buffer = Device.CreateBuffer(&bufferDesc);
    MLG_CHECK(buffer, "Failed to create storage buffer");

    GpuResource* res = m_ResourceAllocator.New();

    MLG_CHECKV(res, "Error allocating DawnGpuStorageBuffer");

    return ::new(&res->StorageBuffer) DawnGpuStorageBuffer(this, buffer);
}

Result<>
DawnGpuDevice::DestroyStorageBuffer(GpuStorageBuffer* storageBuffer)
{
    DawnGpuStorageBuffer* dawnBuffer = static_cast<DawnGpuStorageBuffer*>(storageBuffer);
    MLG_ASSERT(this == dawnBuffer->m_GpuDevice, "StorageBuffer does not belong to this device");
    dawnBuffer->m_GpuDevice = nullptr;
    dawnBuffer->~DawnGpuStorageBuffer();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(storageBuffer));
    return Result<>::Ok;
}

Result<GpuDrawIndirectBuffer*>
DawnGpuDevice::CreateDrawIndirectBuffer(const size_t size)
{
    // Size must be a multiple of 4
    const size_t bufferSize = (size + 3) & ~0x03;

    wgpu::BufferDescriptor bufferDesc //
        {
            .label = "DrawIndirectBuffer",
            .usage = wgpu::BufferUsage::Indirect | wgpu::BufferUsage::CopyDst,
            .size = bufferSize,
            .mappedAtCreation = false,
        };

    wgpu::Buffer buffer = Device.CreateBuffer(&bufferDesc);
    MLG_CHECK(buffer, "Failed to create draw indirect buffer");

    GpuResource* res = m_ResourceAllocator.New();

    MLG_CHECKV(res, "Error allocating DawnGpuDrawIndirectBuffer");

    return ::new(&res->DrawIndirectBuffer) DawnGpuDrawIndirectBuffer(this, buffer);
}

Result<>
DawnGpuDevice::DestroyDrawIndirectBuffer(GpuDrawIndirectBuffer* drawIndirectBuffer)
{
    DawnGpuDrawIndirectBuffer* dawnBuffer =
        static_cast<DawnGpuDrawIndirectBuffer*>(drawIndirectBuffer);
    MLG_ASSERT(this == dawnBuffer->m_GpuDevice, "DrawIndirectBuffer does not belong to this device");
    dawnBuffer->m_GpuDevice = nullptr;
    dawnBuffer->~DawnGpuDrawIndirectBuffer();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(drawIndirectBuffer));
    return Result<>::Ok;
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
    MLG_CHECK(stagingBuffer, "Failed to create staging buffer of size {} for texture upload",
            stagingSize);

    void *mapped = stagingBuffer.GetMappedRange(0, stagingSize);
    MLG_CHECK(mapped, "Failed to map staging buffer for texture upload");

    uint8_t *dst = (uint8_t *)mapped;
    const uint8_t *src = pixels;
    for(uint32_t y = 0; y < height; ++y, dst += alignedRowPitch, src += rowStride)
    {
        ::memcpy(dst, src, width * 4);
    }

    stagingBuffer.Unmap();

    wgpu::CommandEncoderDescriptor encDesc = { .label = name.c_str() };
    wgpu::CommandEncoder encoder = Device.CreateCommandEncoder(&encDesc);
    MLG_CHECK(encoder, "Failed to create command encoder for texture upload");

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
            .format = kTextureFormat,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };

    wgpu::Texture texture = Device.CreateTexture(&textureDesc);
    MLG_CHECK(texture, "Failed to create texture");

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

    MLG_CHECK(commandBuffer, "Failed to finish command buffer for texture upload");

    struct QueueSubmitResult
    {
        std::atomic<bool> done = false;
        Result<> queueSubmitResult = Result<>::Ok;
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
                MLG_ERROR("Queue submit failed: {}", std::string(message.data, message.length));
                result->queueSubmitResult = Result<>::Fail;
            }
            result->done.store(true);
        },
        &result);

    while(!result.done.load())
    {
        Instance.ProcessEvents();
    }

    MLG_CHECK(result.queueSubmitResult);

    auto samplerResult = GetDefaultSampler();
    MLG_CHECK(samplerResult);

    GpuResource* res = m_ResourceAllocator.New();
    MLG_CHECKV(res, "Error allocating DawnGpuTexture");

    auto texView = texture.CreateView();
    MLG_CHECK(texView, "Failed to create texture view for texture");

    return ::new(&res->Texture) DawnGpuTexture(this, texture, texView, *samplerResult);
}

Result<GpuTexture*>
DawnGpuDevice::CreateTexture(const RgbaColorf& color, const imstring& name)
{
    RgbaColoru8 colorU8{color};
    const uint8_t pixelData[4]{colorU8.r, colorU8.g, colorU8.b, colorU8.a};

    return CreateTexture(1, 1, pixelData, 4, name);
}

Result<>
DawnGpuDevice::DestroyTexture(GpuTexture* texture)
{
    DawnGpuTexture* dawnTexture = static_cast<DawnGpuTexture*>(texture);
    MLG_ASSERT(this == dawnTexture->m_GpuDevice, "Texture does not belong to this device");
    dawnTexture->m_GpuDevice = nullptr;
    dawnTexture->~DawnGpuTexture();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(texture));
    return Result<>::Ok;
}

Result<GpuMaterial*>
DawnGpuDevice::CreateMaterial(const MaterialConstants& mtlConstants, GpuTexture* baseTexture)
{
    DawnGpuTexture* dawnBaseTexture = static_cast<DawnGpuTexture*>(baseTexture);

    MLG_CHECKV(dawnBaseTexture->m_GpuDevice == this,
           "Base texture must belong to this device");

    wgpu::Limits gpuLimits;
    Device.GetLimits(&gpuLimits);

    const size_t alignedConstantsSize = alignUniformBuffer<MaterialConstants>(gpuLimits);

    wgpu::BufferDescriptor mtlConstantsBufferDesc //
    {
        .label = "MaterialConstants",
        .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
        .size = alignedConstantsSize,
        .mappedAtCreation = false,
    };

    wgpu::Buffer mtlConstantsBuf = Device.CreateBuffer(&mtlConstantsBufferDesc);
    MLG_CHECK(mtlConstantsBuf, "Failed to create material constants buffer");

    Device.GetQueue().WriteBuffer(mtlConstantsBuf, 0, &mtlConstants, sizeof(MaterialConstants));

    wgpu::BindGroupEntry fsBgEntries[] = //
        {
            {
                .binding = 0,
                .textureView = static_cast<DawnGpuTexture*>(baseTexture)->GetTextureView(),
            },
            {
                .binding = 1,
                .sampler = static_cast<DawnGpuTexture*>(baseTexture)->GetSampler(),
            },
            {
                .binding = 2,
                .buffer = mtlConstantsBuf,
                .offset = 0,
                .size = alignedConstantsSize,
            },
        };

    auto fsBindGroupLayoutResult = GetFsBindGroupLayout();
    MLG_CHECK(fsBindGroupLayoutResult);

    wgpu::BindGroupDescriptor fsBgDesc //
        {
            .label = "fsBindGroup",
            .layout = *fsBindGroupLayoutResult,
            .entryCount = std::size(fsBgEntries),
            .entries = fsBgEntries,
        };

    wgpu::BindGroup bindGroup = Device.CreateBindGroup(&fsBgDesc);
    MLG_CHECK(bindGroup);

    GpuResource* res = m_ResourceAllocator.New();
    MLG_CHECK(res, "Error allocating GpuResource");

    return ::new(&res->Material)
        DawnGpuMaterial(this, baseTexture, mtlConstantsBuf, bindGroup, mtlConstants);
}

Result<>
DawnGpuDevice::DestroyMaterial(GpuMaterial* material)
{
    DawnGpuMaterial* dawnMaterial = static_cast<DawnGpuMaterial*>(material);
    MLG_ASSERT(this == dawnMaterial->m_GpuDevice, "Material does not belong to this device");
    dawnMaterial->m_GpuDevice = nullptr;
    dawnMaterial->~DawnGpuMaterial();
    m_ResourceAllocator.Delete(reinterpret_cast<GpuResource*>(material));
    return Result<>::Ok;
}

Renderer*
DawnGpuDevice::GetRenderer()
{
    return m_Renderer;
}

RenderCompositor*
DawnGpuDevice::GetRenderCompositor()
{
    return m_RenderCompositor;
}

wgpu::TextureFormat
DawnGpuDevice::GetSwapChainFormat() const
{
    return m_SwapChainFormat;
}

//private:

Result<wgpu::Sampler>
DawnGpuDevice::GetDefaultSampler()
{
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
        MLG_CHECK(m_Sampler, "Failed to create sampler");
    }
    return m_Sampler;
}

Result<wgpu::BindGroupLayout>
DawnGpuDevice::GetFsBindGroupLayout()
{
    if(m_FsBindGroupLayout)
    {
        return m_FsBindGroupLayout;
    }

    wgpu::Limits gpuLimits;
    Device.GetLimits(&gpuLimits);

    wgpu::BindGroupLayoutEntry entries[] = //
        {
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Fragment,
                .texture = //
                {
                    .sampleType = wgpu::TextureSampleType::Float,
                    .viewDimension = wgpu::TextureViewDimension::e2D,
                    .multisampled = false,
                },
            },
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Fragment,
                .sampler = //
                {
                    .type = wgpu::SamplerBindingType::Filtering,
                },
            },
            {
                .binding = 2,
                .visibility = wgpu::ShaderStage::Fragment,
                .buffer = //
                {
                    .type = wgpu::BufferBindingType::Uniform,
                    .hasDynamicOffset = false,
                    .minBindingSize = alignUniformBuffer<MaterialConstants>(gpuLimits),
                },
            },
        };

    wgpu::BindGroupLayoutDescriptor layoutDesc //
        {
            .label = "fsBindGroupLayout",
            .entryCount = std::size(entries),
            .entries = entries,
        };

    m_FsBindGroupLayout = Device.CreateBindGroupLayout(&layoutDesc);
    MLG_CHECK(m_FsBindGroupLayout, "Failed to create bind group layout");

    return m_FsBindGroupLayout;
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
            .label = wgpu::StringView{std::string_view{GpuBufferTraits<T>::DebugName}},
            .usage = GpuBufferTraits<T>::Usage,
            .size = sizeofBuffer,
            .mappedAtCreation = true,
        };

    wgpu::Buffer buffer = device.CreateBuffer(&bufferDesc);
    MLG_CHECK(buffer, "Failed to create {} buffer", GpuBufferTraits<T>::DebugName);

    void *p = buffer.GetMappedRange(0,  sizeofBuffer);
    MLG_CHECK(p, "Failed to map {} buffer", GpuBufferTraits<T>::DebugName);

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

#include <dawn/native/DawnNative.h> // provides dawn::native::GetTogglesUsed
#include <iostream>

static void DumpToggles(const char* label, const std::vector<const char*>& toggles)
{
    std::cout << label << " (" << toggles.size() << "):\n";
    for (const char* t : toggles)
    {
        std::cout << "  " << t << "\n";
    }
}

static void DumpDawnToggles(const wgpu::Device& device)
{
    DumpToggles("Device enabled toggles",  dawn::native::GetTogglesUsed(device.Get()));
}