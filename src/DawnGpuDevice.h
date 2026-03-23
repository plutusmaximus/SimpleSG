#pragma once

#include "DawnRenderCompositor.h"
#include "DawnRenderer.h"
#include "GpuDevice.h"
#include "PoolAllocator.h"
#include "Vertex.h"

#include <span>
#include <webgpu/webgpu_cpp.h>

struct SDL_Window;

class DawnGpuVertexBuffer : public GpuVertexBuffer
{
public:

    DawnGpuVertexBuffer(
        DawnGpuDevice* gpuDevice, wgpu::Buffer buffer)
        : m_GpuDevice(gpuDevice),
          m_Buffer(buffer)
    {
    }

    DawnGpuVertexBuffer() = delete;
    DawnGpuVertexBuffer(const DawnGpuVertexBuffer&) = delete;
    DawnGpuVertexBuffer& operator=(const DawnGpuVertexBuffer&) = delete;
    DawnGpuVertexBuffer(DawnGpuVertexBuffer&&) = delete;
    DawnGpuVertexBuffer& operator=(DawnGpuVertexBuffer&&) = delete;

    // wgpu::Buffer is ref counted so nothing to do here.
    ~DawnGpuVertexBuffer() override
    {
        MLG_ASSERT(!m_GpuDevice, "DawnGpuVertexBuffer destroyed while still in use");
    };

    wgpu::Buffer GetBuffer() const { return m_Buffer; }

private:
    friend class DawnGpuDevice;

    DawnGpuDevice* m_GpuDevice;
    wgpu::Buffer m_Buffer;
};

class DawnGpuIndexBuffer : public GpuIndexBuffer
{
public:
    DawnGpuIndexBuffer(
        DawnGpuDevice* gpuDevice, wgpu::Buffer buffer)
        : m_GpuDevice(gpuDevice),
          m_Buffer(buffer)
    {
    }

    DawnGpuIndexBuffer() = delete;
    DawnGpuIndexBuffer(const DawnGpuIndexBuffer&) = delete;
    DawnGpuIndexBuffer& operator=(const DawnGpuIndexBuffer&) = delete;
    DawnGpuIndexBuffer(DawnGpuIndexBuffer&&) = delete;
    DawnGpuIndexBuffer& operator=(DawnGpuIndexBuffer&&) = delete;

    // wgpu::Buffer is ref counted so nothing to do here.
    ~DawnGpuIndexBuffer() override
    {
        MLG_ASSERT(!m_GpuDevice, "DawnGpuIndexBuffer destroyed while still in use");
    };

    wgpu::Buffer GetBuffer() const { return m_Buffer; }

private:
    friend class DawnGpuDevice;

    DawnGpuDevice* m_GpuDevice;
    wgpu::Buffer m_Buffer;
};

class DawnGpuStorageBuffer : public GpuStorageBuffer
{
public:
    DawnGpuStorageBuffer(DawnGpuDevice* gpuDevice, wgpu::Buffer buffer)
        : m_GpuDevice(gpuDevice),
          m_Buffer(buffer)
    {
    }

    DawnGpuStorageBuffer() = delete;
    DawnGpuStorageBuffer(const DawnGpuStorageBuffer&) = delete;
    DawnGpuStorageBuffer& operator=(const DawnGpuStorageBuffer&) = delete;
    DawnGpuStorageBuffer(DawnGpuStorageBuffer&&) = delete;
    DawnGpuStorageBuffer& operator=(DawnGpuStorageBuffer&&) = delete;

    // wgpu::Buffer is ref counted so nothing to do here.
    ~DawnGpuStorageBuffer() override
    {
        MLG_ASSERT(!m_GpuDevice, "DawnGpuStorageBuffer destroyed while still in use");
    };

    Result<> WriteBuffer(const std::span<const uint8_t>& data) override;

    size_t GetSize() const override { return m_Buffer.GetSize(); }

    wgpu::Buffer GetBuffer() const { return m_Buffer; }

private:
    friend class DawnGpuDevice;

    DawnGpuDevice* m_GpuDevice;
    wgpu::Buffer m_Buffer;
};

class DawnGpuDrawIndirectBuffer : public GpuDrawIndirectBuffer
{
public:
    DawnGpuDrawIndirectBuffer(DawnGpuDevice* gpuDevice, wgpu::Buffer buffer)
        : m_GpuDevice(gpuDevice),
          m_Buffer(buffer)
    {
    }

    DawnGpuDrawIndirectBuffer() = delete;
    DawnGpuDrawIndirectBuffer(const DawnGpuDrawIndirectBuffer&) = delete;
    DawnGpuDrawIndirectBuffer& operator=(const DawnGpuDrawIndirectBuffer&) = delete;
    DawnGpuDrawIndirectBuffer(DawnGpuDrawIndirectBuffer&&) = delete;
    DawnGpuDrawIndirectBuffer& operator=(DawnGpuDrawIndirectBuffer&&) = delete;

    // wgpu::Buffer is ref counted so nothing to do here.
    ~DawnGpuDrawIndirectBuffer() override
    {
        MLG_ASSERT(!m_GpuDevice, "DawnGpuDrawIndirectBuffer destroyed while still in use");
    };

    Result<> WriteBuffer(const std::span<const uint8_t>& data) override;

    size_t GetSize() const override { return m_Buffer.GetSize(); }

    wgpu::Buffer GetBuffer() const { return m_Buffer; }

private:
    friend class DawnGpuDevice;

    DawnGpuDevice* m_GpuDevice;
    wgpu::Buffer m_Buffer;
};

class DawnGpuTexture : public GpuTexture
{
public:
    DawnGpuTexture(DawnGpuDevice* gpuDevice,
        wgpu::Texture texture,
        wgpu::TextureView textureView,
        wgpu::Sampler sampler,
        const unsigned width,
        const unsigned height)
        : m_GpuDevice(gpuDevice),
          m_Texture(texture),
          m_TextureView(textureView),
          m_Sampler(sampler),
          m_Width(width),
          m_Height(height)
    {
    }

    DawnGpuTexture() = delete;
    DawnGpuTexture(const DawnGpuTexture&) = delete;
    DawnGpuTexture& operator=(const DawnGpuTexture&) = delete;
    DawnGpuTexture(DawnGpuTexture&&) = delete;
    DawnGpuTexture& operator=(DawnGpuTexture&&) = delete;

    // wgpu::Texture is ref counted so nothing to do here.
    ~DawnGpuTexture() override
    {
        MLG_ASSERT(!m_GpuDevice, "DawnGpuTexture destroyed while still in use");
    };

    unsigned GetWidth() const override { return m_Width; }
    unsigned GetHeight() const override { return m_Height; }

    wgpu::Texture GetTexture() const { return m_Texture; }
    wgpu::TextureView GetTextureView() const { return m_TextureView; }
    wgpu::Sampler GetSampler() const { return m_Sampler; }

private:
    friend class DawnGpuDevice;

    DawnGpuDevice* m_GpuDevice;
    wgpu::Texture m_Texture;
    wgpu::TextureView m_TextureView;
    wgpu::Sampler m_Sampler;
    unsigned m_Width;
    unsigned m_Height;
};

class DawnGpuMaterial : public GpuMaterial
{
public:

    DawnGpuMaterial(DawnGpuDevice* gpuDevice,
        GpuTexture* baseTexture,
        wgpu::Buffer constantsBuffer,
        wgpu::BindGroup bindGroup,
        const MaterialConstants& constants)
        : m_GpuDevice(gpuDevice),
          m_BaseTexture(baseTexture),
          m_ConstantsBuffer(constantsBuffer),
          m_BindGroup(bindGroup),
          m_Constants(constants)

    {
    }

    DawnGpuMaterial() = delete;
    DawnGpuMaterial(const DawnGpuMaterial&) = delete;
    DawnGpuMaterial& operator=(const DawnGpuMaterial&) = delete;
    DawnGpuMaterial(DawnGpuMaterial&&) = delete;
    DawnGpuMaterial& operator=(DawnGpuMaterial&&) = delete;

    ~DawnGpuMaterial() override
    {
        MLG_ASSERT(!m_GpuDevice, "DawnGpuMaterial destroyed while still in use");
    }

    GpuTexture* GetBaseTexture() const override { return m_BaseTexture; }

    const MaterialConstants& GetConstants() const override { return m_Constants; }

    wgpu::BindGroup GetBindGroup() const { return m_BindGroup; }

private:
    friend class DawnGpuDevice;

    DawnGpuDevice* m_GpuDevice;
    GpuTexture* m_BaseTexture;
    wgpu::Buffer m_ConstantsBuffer;
    wgpu::BindGroup m_BindGroup;
    MaterialConstants m_Constants;
};

class DawnGpuColorTarget : public GpuColorTarget
{
public:
    DawnGpuColorTarget(DawnGpuDevice* gpuDevice,
        wgpu::Texture texture,
        wgpu::TextureView textureView,
        wgpu::Sampler sampler,
        const unsigned width,
        const unsigned height,
        wgpu::TextureFormat format)
        : m_GpuDevice(gpuDevice),
          m_Texture(texture),
          m_TextureView(textureView),
          m_Sampler(sampler),
          m_Width(width),
          m_Height(height),
          m_Format(format)
    {
    }

    DawnGpuColorTarget() = delete;
    DawnGpuColorTarget(const DawnGpuColorTarget&) = delete;
    DawnGpuColorTarget& operator=(const DawnGpuColorTarget&) = delete;
    DawnGpuColorTarget(DawnGpuColorTarget&&) = delete;
    DawnGpuColorTarget& operator=(DawnGpuColorTarget&&) = delete;

    // wgpu::Texture is ref counted so nothing to do here.
    ~DawnGpuColorTarget() override
    {
        MLG_ASSERT(!m_GpuDevice, "DawnGpuColorTarget destroyed while still in use");
    }

    unsigned GetWidth() const override { return m_Width; }
    unsigned GetHeight() const override { return m_Height; }
    wgpu::TextureFormat GetFormat() const { return m_Format; }

    wgpu::Texture GetTexture() const { return m_Texture; }
    wgpu::TextureView GetTextureView() const { return m_TextureView; }
    wgpu::Sampler GetSampler() const { return m_Sampler; }

private:
    friend class DawnGpuDevice;

    DawnGpuDevice* m_GpuDevice;
    wgpu::Texture m_Texture;
    wgpu::TextureView m_TextureView;
    wgpu::Sampler m_Sampler;
    unsigned m_Width;
    unsigned m_Height;
    wgpu::TextureFormat m_Format;
};

class DawnGpuDepthTarget : public GpuDepthTarget
{
public:
    DawnGpuDepthTarget(DawnGpuDevice* gpuDevice,
        wgpu::Texture depthTarget,
        wgpu::TextureView depthTargetView,
        const unsigned width,
        const unsigned height,
        wgpu::TextureFormat format)
        : m_GpuDevice(gpuDevice),
          m_DepthTarget(depthTarget),
          m_DepthTargetView(depthTargetView),
          m_Width(width),
          m_Height(height),
          m_Format(format)
    {
    }

    DawnGpuDepthTarget() = delete;
    DawnGpuDepthTarget(const DawnGpuDepthTarget&) = delete;
    DawnGpuDepthTarget& operator=(const DawnGpuDepthTarget&) = delete;
    DawnGpuDepthTarget(DawnGpuDepthTarget&&) = delete;
    DawnGpuDepthTarget& operator=(DawnGpuDepthTarget&&) = delete;

    // wgpu::Texture is ref counted so nothing to do here.
    ~DawnGpuDepthTarget() override
    {
        MLG_ASSERT(!m_GpuDevice, "DawnGpuDepthTarget destroyed while still in use");
    }

    unsigned GetWidth() const override { return m_Width; }
    unsigned GetHeight() const override { return m_Height; }
    wgpu::TextureFormat GetFormat() const { return m_Format; }

    wgpu::Texture GetTexture() const { return m_DepthTarget; }
    wgpu::TextureView GetTextureView() const { return m_DepthTargetView; }

private:
    friend class DawnGpuDevice;

    DawnGpuDevice* m_GpuDevice;
    wgpu::Texture m_DepthTarget;
    wgpu::TextureView m_DepthTargetView;
    unsigned m_Width;
    unsigned m_Height;
    wgpu::TextureFormat m_Format;
};

/// @brief Dawn GPU Device implementation.
class DawnGpuDevice : public GpuDevice
{
public:
    static Result<GpuDevice*> Create(SDL_Window* window);

    static void Destroy(GpuDevice* device);

    DawnGpuDevice() = delete;
    DawnGpuDevice(const DawnGpuDevice&) = delete;
    DawnGpuDevice& operator=(const DawnGpuDevice&) = delete;
    DawnGpuDevice(DawnGpuDevice&&) = delete;
    DawnGpuDevice& operator=(DawnGpuDevice&&) = delete;

    ~DawnGpuDevice() override;

    /// @brief Gets the renderable extent of the device.
    Extent GetScreenBounds() const override;

    Result<GpuVertexBuffer*> CreateVertexBuffer(const std::span<const Vertex>& vertices) override;

    Result<GpuVertexBuffer*> CreateVertexBuffer(
        const std::span<std::span<const Vertex>>& vertices) override;

    Result<> DestroyVertexBuffer(GpuVertexBuffer* buffer) override;

    Result<GpuIndexBuffer*> CreateIndexBuffer(const std::span<const VertexIndex>& indices) override;

    Result<GpuIndexBuffer*> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) override;

    Result<> DestroyIndexBuffer(GpuIndexBuffer* buffer) override;

    Result<GpuStorageBuffer*> CreateStorageBuffer(const size_t size) override;

    Result<> DestroyStorageBuffer(GpuStorageBuffer* storageBuffer) override;

    Result<GpuDrawIndirectBuffer*> CreateDrawIndirectBuffer(const size_t size) override;

    Result<> DestroyDrawIndirectBuffer(GpuDrawIndirectBuffer* drawIndirectBuffer) override;

    Result<GpuTexture*> CreateTexture(const unsigned width,
        const unsigned height,
        const uint8_t* pixels,
        const unsigned rowStride,
        const imstring& name) override;

    Result<GpuTexture*> CreateTexture(const RgbaColorf& color, const imstring& name) override;

    Result<> DestroyTexture(GpuTexture* texture) override;

    Result<GpuColorTarget*> CreateColorTarget(
        const unsigned width, const unsigned height, const imstring& name) override;

    Result<> DestroyColorTarget(GpuColorTarget* colorTarget) override;

    Result<GpuDepthTarget*> CreateDepthTarget(const unsigned width,
        const unsigned height,
        const imstring& name) override;

    Result<> DestroyDepthTarget(GpuDepthTarget* depthTarget) override;

    Result<GpuMaterial*> CreateMaterial(const MaterialConstants& constants,
        GpuTexture* baseTexture) override;

    Result<> DestroyMaterial(GpuMaterial* material) override;

    Renderer* GetRenderer() override;

    RenderCompositor* GetRenderCompositor() override;

    wgpu::TextureFormat GetSwapChainFormat() const;

    SDL_Window* const Window;
    wgpu::Instance const Instance;
    wgpu::Adapter const Adapter;
    wgpu::Device const Device;
    wgpu::Surface const Surface;

private:
    DawnGpuDevice(SDL_Window* window,
        wgpu::Instance instance,
        wgpu::Adapter adapter,
        wgpu::Device device,
        wgpu::Surface surface,
        const wgpu::TextureFormat surfaceFormat);

    Result<wgpu::Sampler> GetDefaultSampler();

    /// @brief Gets the default bind group layout for fragment shaders.
    Result<wgpu::BindGroupLayout> GetFsBindGroupLayout();

    /// @brief Default sampler used for all textures.
    wgpu::Sampler m_Sampler;

    /// @brief Default bind group layout for fragment shaders.
    wgpu::BindGroupLayout m_FsBindGroupLayout;

    union GpuResource
    {
        GpuResource() {}
        ~GpuResource() {}

        DawnGpuVertexBuffer VertexBuffer;
        DawnGpuIndexBuffer IndexBuffer;
        DawnGpuStorageBuffer StorageBuffer;
        DawnGpuDrawIndirectBuffer DrawIndirectBuffer;
        DawnGpuTexture Texture;
        DawnGpuMaterial Material;
        DawnGpuColorTarget ColorTarget;
        DawnGpuDepthTarget DepthTarget;
    };

    PoolAllocator<GpuResource, 256> m_ResourceAllocator;

    // Renderer and RenderCompositor are initialized with ::new(), so
    // they can be destroyed explicitly before the GPU device is destroyed (as they hold GPU
    // resources and must be destroyed first).
    uint8_t m_RendererBuffer[sizeof(DawnRenderer)];
    uint8_t m_RenderCompositorBuffer[sizeof(DawnRenderCompositor)];

    DawnRenderer* m_Renderer{nullptr};
    DawnRenderCompositor* m_RenderCompositor{nullptr};

    const wgpu::TextureFormat m_SwapChainFormat{ wgpu::TextureFormat::Undefined };
};