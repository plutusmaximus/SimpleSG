#include "DawnGpuDevice.h"

#include "Log.h"
#include "Material.h"
#include "WebgpuHelper.h"

#include <SDL3/SDL.h>
#include <webgpu/webgpu_cpp.h>

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

static wgpu::TextureFormat GetSurfaceFormat(const wgpu::Surface& surface)
{
    wgpu::SurfaceTexture surfaceTexture;
    surface.GetCurrentTexture(&surfaceTexture);
    MLG_ASSERT(surfaceTexture.texture, "Failed to acquire current surface texture");
    return surfaceTexture.texture.GetFormat();
}

DawnGpuDevice::DawnGpuDevice(
    SDL_Window* window, wgpu::Instance instance, wgpu::Device device, wgpu::Surface surface)
    : Window(window),
      Instance(instance),
      Device(device),
      Surface(surface),
      m_SwapChainFormat(GetSurfaceFormat(Surface))
{
    auto rendererResult = DawnRenderer::Create(this);
    m_Renderer = *rendererResult;
    m_RenderCompositor = ::new(m_RenderCompositorBuffer)DawnRenderCompositor(this);
}

Result<GpuDevice*>
DawnGpuDevice::Create()
{
    MLG_INFO("Creating Dawn GPU Device...");

    DawnGpuDevice* dawnDevice = new DawnGpuDevice(WebgpuHelper::GetWindow(),
        WebgpuHelper::GetInstance(),
        WebgpuHelper::GetDevice(),
        WebgpuHelper::GetSurface());
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
    const size_t sizeofBuffer = std::ranges::fold_left(vertices, 0, [](size_t sum, const std::span<const Vertex>& span)
    {
        return sum + span.size() * sizeof(Vertex);
    });

    auto buffer = WebgpuHelper::CreateVertexBuffer(sizeofBuffer, "VertexBuffer");
    MLG_CHECK(buffer, "Failed to create vertex buffer");

    uint64_t dstOffset = 0;
    wgpu::Queue queue = WebgpuHelper::GetDevice().GetQueue();

    for(const auto& span : vertices)
    {
        if(span.empty())
        {
            continue;
        }

        const size_t spanLen = span.size();
        const size_t dataSize = spanLen * sizeof(span[0]);

        queue.WriteBuffer(*buffer, dstOffset, span.data(), dataSize);
        dstOffset += dataSize;
    }

    GpuResource* res = m_ResourceAllocator.New();

    MLG_CHECKV(res, "Error allocating DawnGpuVertexBuffer");

    return ::new(&res->VertexBuffer) DawnGpuVertexBuffer(this, *buffer);
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
    const size_t sizeofBuffer = std::ranges::fold_left(indices, 0, [](size_t sum, const std::span<const VertexIndex>& span)
    {
        return sum + span.size() * sizeof(VertexIndex);
    });

    auto buffer = WebgpuHelper::CreateIndexBuffer(sizeofBuffer, "IndexBuffer");
    MLG_CHECK(buffer, "Failed to create index buffer");

    uint64_t dstOffset = 0;
    wgpu::Queue queue = WebgpuHelper::GetDevice().GetQueue();

    for(const auto& span : indices)
    {
        if(span.empty())
        {
            continue;
        }

        const size_t spanLen = span.size();
        const size_t dataSize = spanLen * sizeof(span[0]);

        queue.WriteBuffer(*buffer, dstOffset, span.data(), dataSize);
        dstOffset += dataSize;
    }

    GpuResource* res = m_ResourceAllocator.New();

    MLG_CHECKV(res, "Error allocating DawnGpuIndexBuffer");

    return ::new(&res->IndexBuffer) DawnGpuIndexBuffer(this, *buffer);
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
    auto texture = WebgpuHelper::CreateTexture(width, height, pixels, rowStride, name);
    MLG_CHECK(texture);

    auto samplerResult = WebgpuHelper::GetDefaultSampler();
    MLG_CHECK(samplerResult);

    GpuResource* res = m_ResourceAllocator.New();
    MLG_CHECKV(res, "Error allocating DawnGpuTexture");

    auto texView = texture->CreateView();
    MLG_CHECK(texView, "Failed to create texture view for texture");

    return ::new(&res->Texture) DawnGpuTexture(this, *texture, texView, *samplerResult);
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