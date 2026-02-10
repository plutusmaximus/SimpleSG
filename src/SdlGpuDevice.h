#pragma once

#include "GpuDevice.h"
#include "Material.h"
#include "PoolAllocator.h"
#include "Vertex.h"

#include <map>
#include <span>

struct SDL_Window;
struct SDL_GPUDevice;
struct SDL_GPUBuffer;
struct SDL_GPUTexture;
struct SDL_GPUSampler;
struct SDL_GPUShader;
struct SDL_GPUGraphicsPipeline;

class SdlGpuVertexBuffer : public GpuVertexBuffer
{
    class SdlSubrange : public Subrange
    {
    public:
        SdlSubrange(SdlGpuVertexBuffer* owner, const uint32_t itemOffset, const uint32_t itemCount)
            : Subrange(owner, itemOffset, itemCount)
        {
        }
    };

public:

    SdlGpuVertexBuffer() = delete;
    SdlGpuVertexBuffer(const SdlGpuVertexBuffer&) = delete;
    SdlGpuVertexBuffer& operator=(const SdlGpuVertexBuffer&) = delete;
    SdlGpuVertexBuffer(SdlGpuVertexBuffer&&) = delete;
    SdlGpuVertexBuffer& operator=(SdlGpuVertexBuffer&&) = delete;

    ~SdlGpuVertexBuffer() override;

    SDL_GPUBuffer* GetBuffer() const { return m_Buffer; }

    Subrange GetSubrange(const uint32_t itemOffset, const uint32_t itemCount) override
    {
        eassert(itemOffset + itemCount <= m_ItemCount, "Sub-range out of bounds");
        return SdlSubrange(this, itemOffset, itemCount);
    }

private:

    friend class SdlGpuDevice;

    SdlGpuVertexBuffer(SDL_GPUDevice* gpuDevice, SDL_GPUBuffer* buffer, const uint32_t itemCount)
        : m_Buffer(buffer)
        , m_GpuDevice(gpuDevice)
        , m_ItemCount(itemCount)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
    SDL_GPUBuffer* const m_Buffer;
    const uint32_t m_ItemCount;
};

class SdlGpuIndexBuffer : public GpuIndexBuffer
{
    class SdlSubrange : public Subrange
    {
    public:
        SdlSubrange(SdlGpuIndexBuffer* owner, const uint32_t itemOffset, const uint32_t itemCount)
            : Subrange(owner, itemOffset, itemCount)
        {
        }
    };

public:

    SdlGpuIndexBuffer() = delete;
    SdlGpuIndexBuffer(const SdlGpuIndexBuffer&) = delete;
    SdlGpuIndexBuffer& operator=(const SdlGpuIndexBuffer&) = delete;
    SdlGpuIndexBuffer(SdlGpuIndexBuffer&&) = delete;
    SdlGpuIndexBuffer& operator=(SdlGpuIndexBuffer&&) = delete;

    ~SdlGpuIndexBuffer() override;

    SDL_GPUBuffer* GetBuffer() const { return m_Buffer; }

    Subrange GetSubrange(const uint32_t itemOffset, const uint32_t itemCount) override
    {
        eassert(itemOffset + itemCount <= m_ItemCount, "Sub-range out of bounds");
        return SdlSubrange(this, itemOffset, itemCount);
    }

private:

    friend class SdlGpuDevice;

    SdlGpuIndexBuffer(SDL_GPUDevice* gpuDevice, SDL_GPUBuffer* buffer, const uint32_t itemCount)
        : m_Buffer(buffer)
        , m_GpuDevice(gpuDevice)
        , m_ItemCount(itemCount)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
    SDL_GPUBuffer* const m_Buffer;
    const uint32_t m_ItemCount;
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

    SDL_GPUTexture* GetTexture() const { return m_Texture; }
    SDL_GPUSampler* GetSampler() const { return m_Sampler; }

private:

    friend class SdlGpuDevice;

    SdlGpuTexture(SDL_GPUDevice* gpuDevice, SDL_GPUTexture* texture, SDL_GPUSampler* sampler)
        : m_GpuDevice(gpuDevice)
        , m_Texture(texture)
        , m_Sampler(sampler)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
    SDL_GPUTexture* const m_Texture;
    SDL_GPUSampler* const m_Sampler;
};

class SdlGpuVertexShader : public GpuVertexShader
{
public:

    SdlGpuVertexShader() = delete;
    SdlGpuVertexShader(const SdlGpuVertexShader&) = delete;
    SdlGpuVertexShader& operator=(const SdlGpuVertexShader&) = delete;
    SdlGpuVertexShader(SdlGpuVertexShader&&) = delete;
    SdlGpuVertexShader& operator=(SdlGpuVertexShader&&) = delete;

    ~SdlGpuVertexShader() override;

    SDL_GPUShader* GetShader() const { return m_Shader; }

private:

    friend class SdlGpuDevice;

    SdlGpuVertexShader(SDL_GPUDevice* gpuDevice, SDL_GPUShader* shader)
        : m_GpuDevice(gpuDevice)
        , m_Shader(shader)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
    SDL_GPUShader* const m_Shader;
};

class SdlGpuFragmentShader : public GpuFragmentShader
{
public:

    SdlGpuFragmentShader() = delete;
    SdlGpuFragmentShader(const SdlGpuFragmentShader&) = delete;
    SdlGpuFragmentShader& operator=(const SdlGpuFragmentShader&) = delete;
    SdlGpuFragmentShader(SdlGpuFragmentShader&&) = delete;
    SdlGpuFragmentShader& operator=(SdlGpuFragmentShader&&) = delete;

    ~SdlGpuFragmentShader() override;

    SDL_GPUShader* GetShader() const { return m_Shader; }

private:

    friend class SdlGpuDevice;

    SdlGpuFragmentShader(SDL_GPUDevice* gpuDevice, SDL_GPUShader* shader)
        : m_GpuDevice(gpuDevice)
        , m_Shader(shader)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
    SDL_GPUShader* const m_Shader;
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

    Extent GetExtent() const override;

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

    Result<GpuVertexShader*> CreateVertexShader(const VertexShaderSpec& shaderSpec) override;

    Result<GpuVertexShader*> CreateVertexShader(const std::span<const uint8_t>& shaderCode) override;

    Result<void> DestroyVertexShader(GpuVertexShader* shader) override;

    Result<GpuFragmentShader*> CreateFragmentShader(const FragmentShaderSpec& shaderSpec) override;

    Result<GpuFragmentShader*> CreateFragmentShader(const std::span<const uint8_t>& shaderCode) override;

    Result<void> DestroyFragmentShader(GpuFragmentShader* shader) override;

    Result<RenderGraph*> CreateRenderGraph() override;

    void DestroyRenderGraph(RenderGraph* renderGraph) override;

    Result<SDL_GPUGraphicsPipeline*> GetOrCreatePipeline(const Material& mtl);

    SDL_Window* const Window;
    SDL_GPUDevice* const Device;

private:

    SdlGpuDevice(SDL_Window* window, SDL_GPUDevice* gpuDevice);

    struct PipelineKey
    {
        const int ColorFormat;
        SDL_GPUShader* const VertexShader;
        SDL_GPUShader* const FragShader;

        bool operator<(const PipelineKey& other) const
        {
            return std::memcmp(this, &other, sizeof(*this)) < 0;
        }
    };

    std::map<PipelineKey, SDL_GPUGraphicsPipeline*> m_PipelinesByKey;

    /// @brief Default sampler used for all textures.
    SDL_GPUSampler* m_Sampler = nullptr;

    union GpuResource
    {
        GpuResource() {}
        ~GpuResource() {}

        SdlGpuVertexBuffer VertexBuffer;
        SdlGpuIndexBuffer IndexBuffer;
        SdlGpuTexture Texture;
        SdlGpuVertexShader VertexShader;
        SdlGpuFragmentShader FragmentShader;
    };

    PoolAllocator<GpuResource, 256> m_ResourceAllocator;
};