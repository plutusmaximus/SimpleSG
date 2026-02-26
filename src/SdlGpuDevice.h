#pragma once

#include "GpuDevice.h"
#include "PoolAllocator.h"
#include "Vertex.h"
#include "SdlRenderCompositor.h"
#include "SdlRenderer.h"

#include <span>

#include <SDL3/SDL.h>

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUBuffer;
struct SDL_GPUTexture;
struct SDL_GPUSampler;
struct SDL_GPUShader;
struct SDL_GPUGraphicsPipeline;

class SdlGpuVertexBuffer : public GpuVertexBuffer
{
public:

    SdlGpuVertexBuffer() = delete;
    SdlGpuVertexBuffer(const SdlGpuVertexBuffer&) = delete;
    SdlGpuVertexBuffer& operator=(const SdlGpuVertexBuffer&) = delete;
    SdlGpuVertexBuffer(SdlGpuVertexBuffer&&) = delete;
    SdlGpuVertexBuffer& operator=(SdlGpuVertexBuffer&&) = delete;

    ~SdlGpuVertexBuffer() override;

    SDL_GPUBuffer* GetBuffer() const { return m_Buffer; }

    unsigned GetVertexCount() const override { return m_ItemCount; }

private:

    friend class SdlGpuDevice;

    SdlGpuVertexBuffer(SdlGpuDevice* gpuDevice, SDL_GPUBuffer* buffer, const unsigned itemCount)
        : m_Buffer(buffer)
        , m_GpuDevice(gpuDevice)
        , m_ItemCount(itemCount)
    {
    }

    SdlGpuDevice* const m_GpuDevice;
    SDL_GPUBuffer* const m_Buffer;
    const unsigned m_ItemCount;
};

class SdlGpuIndexBuffer : public GpuIndexBuffer
{
public:

    SdlGpuIndexBuffer() = delete;
    SdlGpuIndexBuffer(const SdlGpuIndexBuffer&) = delete;
    SdlGpuIndexBuffer& operator=(const SdlGpuIndexBuffer&) = delete;
    SdlGpuIndexBuffer(SdlGpuIndexBuffer&&) = delete;
    SdlGpuIndexBuffer& operator=(SdlGpuIndexBuffer&&) = delete;

    ~SdlGpuIndexBuffer() override;

    SDL_GPUBuffer* GetBuffer() const { return m_Buffer; }

    unsigned GetIndexCount() const override { return m_ItemCount; }

private:

    friend class SdlGpuDevice;

    SdlGpuIndexBuffer(SdlGpuDevice* gpuDevice, SDL_GPUBuffer* buffer, const unsigned itemCount)
        : m_Buffer(buffer)
        , m_GpuDevice(gpuDevice)
        , m_ItemCount(itemCount)
    {
    }

    SdlGpuDevice* const m_GpuDevice;
    SDL_GPUBuffer* const m_Buffer;
    const unsigned m_ItemCount;
};

class SdlGpuTexture : public GpuTexture
{
public:

    SdlGpuTexture() = delete;
    SdlGpuTexture(const SdlGpuTexture&) = delete;
    SdlGpuTexture& operator=(const SdlGpuTexture&) = delete;
    SdlGpuTexture(SdlGpuTexture&&) = delete;
    SdlGpuTexture& operator=(SdlGpuTexture&&) = delete;

    ~SdlGpuTexture() override;

    unsigned GetWidth() const override { return m_Width; }
    unsigned GetHeight() const override { return m_Height; }

    SDL_GPUTexture* GetTexture() const { return m_Texture; }
    SDL_GPUSampler* GetSampler() const { return m_Sampler; }

private:

    friend class SdlGpuDevice;

    SdlGpuTexture(SdlGpuDevice* gpuDevice,
        SDL_GPUTexture* texture,
        SDL_GPUSampler* sampler,
        const unsigned width,
        const unsigned height)
        : m_GpuDevice(gpuDevice),
          m_Texture(texture),
          m_Sampler(sampler),
          m_Width(width),
          m_Height(height)
    {
    }

    SdlGpuDevice* const m_GpuDevice;
    SDL_GPUTexture* const m_Texture;
    SDL_GPUSampler* const m_Sampler;
    unsigned m_Width;
    unsigned m_Height;
};

class SdlGpuMaterial : public GpuMaterial
{
public:

    SdlGpuMaterial() = delete;
    SdlGpuMaterial(const SdlGpuMaterial&) = delete;
    SdlGpuMaterial& operator=(const SdlGpuMaterial&) = delete;
    SdlGpuMaterial(SdlGpuMaterial&&) = delete;
    SdlGpuMaterial& operator=(SdlGpuMaterial&&) = delete;

    ~SdlGpuMaterial() override {};

    GpuTexture* GetBaseTexture() const override { return m_BaseTexture; }

    const MaterialConstants& GetConstants() const override { return m_Constants; }

private:
    friend class SdlGpuDevice;

    SdlGpuMaterial(SdlGpuDevice* gpuDevice,
        GpuTexture* baseTexture,
        const MaterialConstants& constants)
        : m_GpuDevice(gpuDevice),
          m_BaseTexture(baseTexture),
          m_Constants(constants)

    {
    }

    SdlGpuDevice* const m_GpuDevice;
    GpuTexture* m_BaseTexture;
    MaterialConstants m_Constants;
};

class SdlGpuColorTarget : public GpuColorTarget
{
public:

    SdlGpuColorTarget() = delete;
    SdlGpuColorTarget(const SdlGpuColorTarget&) = delete;
    SdlGpuColorTarget& operator=(const SdlGpuColorTarget&) = delete;
    SdlGpuColorTarget(SdlGpuColorTarget&&) = delete;
    SdlGpuColorTarget& operator=(SdlGpuColorTarget&&) = delete;

    ~SdlGpuColorTarget() override;

    unsigned GetWidth() const override { return m_Width; }
    unsigned GetHeight() const override { return m_Height; }
    SDL_GPUTextureFormat GetFormat() const { return m_Format; }

    SDL_GPUTexture* GetColorTarget() const { return m_ColorTarget; }
    SDL_GPUSampler* GetSampler() const { return m_Sampler; }

private:
    friend class SdlGpuDevice;

    SdlGpuColorTarget(SdlGpuDevice* gpuDevice,
        SDL_GPUTexture* colorTarget,
        SDL_GPUSampler* sampler,
        const unsigned width,
        const unsigned height,
        SDL_GPUTextureFormat format)
        : m_GpuDevice(gpuDevice),
          m_ColorTarget(colorTarget),
          m_Sampler(sampler),
          m_Width(width),
          m_Height(height),
          m_Format(format)
    {
    }

    SdlGpuDevice* const m_GpuDevice;
    SDL_GPUTexture* const m_ColorTarget;
    SDL_GPUSampler* const m_Sampler;
    unsigned m_Width;
    unsigned m_Height;
    SDL_GPUTextureFormat m_Format;
};

class SdlGpuDepthTarget : public GpuDepthTarget
{
public:

    SdlGpuDepthTarget() = delete;
    SdlGpuDepthTarget(const SdlGpuDepthTarget&) = delete;
    SdlGpuDepthTarget& operator=(const SdlGpuDepthTarget&) = delete;
    SdlGpuDepthTarget(SdlGpuDepthTarget&&) = delete;
    SdlGpuDepthTarget& operator=(SdlGpuDepthTarget&&) = delete;

    ~SdlGpuDepthTarget() override;

    unsigned GetWidth() const override { return m_Width; }
    unsigned GetHeight() const override { return m_Height; }
    SDL_GPUTextureFormat GetFormat() const { return m_Format; }

    SDL_GPUTexture* GetDepthTarget() const { return m_DepthTarget; }

private:

    friend class SdlGpuDevice;

    SdlGpuDepthTarget(SdlGpuDevice* gpuDevice,
        SDL_GPUTexture* depthTarget,
        const unsigned width,
        const unsigned height,
        SDL_GPUTextureFormat format)
        : m_GpuDevice(gpuDevice),
          m_DepthTarget(depthTarget),
          m_Width(width),
          m_Height(height),
          m_Format(format)
    {
    }

    SdlGpuDevice* const m_GpuDevice;
    SDL_GPUTexture* const m_DepthTarget;
    unsigned m_Width;
    unsigned m_Height;
    SDL_GPUTextureFormat m_Format;
};

/// @brief SDL GPU Device implementation.
class SdlGpuDevice : public GpuDevice
{
public:

    static Result<GpuDevice*> Create(SDL_Window* window);

    static void Destroy(GpuDevice* device);

    SdlGpuDevice() = delete;
    SdlGpuDevice(const SdlGpuDevice&) = delete;
    SdlGpuDevice& operator=(const SdlGpuDevice&) = delete;
    SdlGpuDevice(SdlGpuDevice&&) = delete;
    SdlGpuDevice& operator=(SdlGpuDevice&&) = delete;

    ~SdlGpuDevice() override;

    Extent GetScreenBounds() const override;

    Result<GpuVertexBuffer*> CreateVertexBuffer(
        const std::span<const Vertex>& vertices) override;

    Result<GpuVertexBuffer*> CreateVertexBuffer(
        const std::span<std::span<const Vertex>>& vertices) override;

    Result<void> DestroyVertexBuffer(GpuVertexBuffer* buffer) override;

    Result<GpuIndexBuffer*> CreateIndexBuffer(
        const std::span<const VertexIndex>& indices) override;

    Result<GpuIndexBuffer*> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) override;

    Result<void> DestroyIndexBuffer(GpuIndexBuffer* buffer) override;

    Result<GpuTexture*> CreateTexture(const unsigned width,
        const unsigned height,
        const uint8_t* pixels,
        const unsigned rowStride,
        const imstring& name) override;

    Result<GpuTexture*> CreateTexture(const RgbaColorf& color, const imstring& name) override;

    Result<void> DestroyTexture(GpuTexture* texture) override;

    Result<GpuColorTarget*> CreateColorTarget(
        const unsigned width, const unsigned height, const imstring& name) override;

    Result<void> DestroyColorTarget(GpuColorTarget* colorTarget) override;

    Result<GpuDepthTarget*> CreateDepthTarget(const unsigned width,
        const unsigned height,
        const imstring& name) override;

    Result<void> DestroyDepthTarget(GpuDepthTarget* depthTarget) override;

    Result<GpuMaterial*> CreateMaterial(const MaterialConstants& constants,
        GpuTexture* baseTexture) override;

    Result<void> DestroyMaterial(GpuMaterial* material) override;

    Renderer* GetRenderer() override;

    RenderCompositor* GetRenderCompositor() override;

    SDL_GPUTextureFormat GetSwapChainFormat() const;

    SDL_Window* const Window;
    SDL_GPUDevice* const Device;

private:

    SdlGpuDevice(SDL_Window* window, SDL_GPUDevice* gpuDevice);

    Result<SDL_GPUSampler*> GetDefaultSampler();

    /// @brief Default sampler used for all textures.
    SDL_GPUSampler* m_Sampler = nullptr;

    union GpuResource
    {
        GpuResource() {}
        ~GpuResource() {}

        SdlGpuVertexBuffer VertexBuffer;
        SdlGpuIndexBuffer IndexBuffer;
        SdlGpuTexture Texture;
        SdlGpuMaterial Material;
        SdlGpuColorTarget ColorTarget;
        SdlGpuDepthTarget DepthTarget;
    };

    PoolAllocator<GpuResource, 256> m_ResourceAllocator;

    // Renderer and RenderCompositor are initialized with ::new(), so
    // theny can be destroyed explicitly before the GPU device is destroyed (as they hold GPU
    // resources and must be destroyed first).
    uint8_t m_RendererBuffer[sizeof(SdlRenderer)];
    uint8_t m_RenderCompositorBuffer[sizeof(SdlRenderCompositor)];

    SdlRenderer* m_Renderer{nullptr};
    SdlRenderCompositor* m_RenderCompositor{nullptr};
};