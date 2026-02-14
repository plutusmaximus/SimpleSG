#pragma once

#include "GpuDevice.h"
#include "Material.h"
#include "PoolAllocator.h"
#include "Vertex.h"

#include <map>
#include <span>
#include <webgpu/webgpu_cpp.h>

struct SDL_Window;

class DawnGpuVertexBuffer : public GpuVertexBuffer
{
    class SdlSubrange : public Subrange
    {
    public:
        SdlSubrange(DawnGpuVertexBuffer* owner, const uint32_t itemOffset, const uint32_t itemCount)
            : Subrange(owner, itemOffset, itemCount)
        {
        }
    };

public:
    DawnGpuVertexBuffer() = delete;
    DawnGpuVertexBuffer(const DawnGpuVertexBuffer&) = delete;
    DawnGpuVertexBuffer& operator=(const DawnGpuVertexBuffer&) = delete;
    DawnGpuVertexBuffer(DawnGpuVertexBuffer&&) = default;
    DawnGpuVertexBuffer& operator=(DawnGpuVertexBuffer&&) = default;

    // wgpu::Buffer is ref counted so nothing to do here.
    ~DawnGpuVertexBuffer() override {};

    wgpu::Buffer GetBuffer() const { return m_Buffer; }

    Subrange GetSubrange(const uint32_t itemOffset, const uint32_t itemCount) override
    {
        eassert(itemOffset + itemCount <= m_ItemCount, "Sub-range out of bounds");
        return SdlSubrange(this, itemOffset, itemCount);
    }

private:
    friend class DawnGpuDevice;

    explicit DawnGpuVertexBuffer(wgpu::Buffer buffer, const uint32_t itemCount)
        : m_Buffer(buffer),
          m_ItemCount(itemCount)
    {
    }

    wgpu::Buffer m_Buffer;
    const uint32_t m_ItemCount;
};

class DawnGpuIndexBuffer : public GpuIndexBuffer
{
    class SdlSubrange : public Subrange
    {
    public:
        SdlSubrange(DawnGpuIndexBuffer* owner, const uint32_t itemOffset, const uint32_t itemCount)
            : Subrange(owner, itemOffset, itemCount)
        {
        }
    };

public:
    DawnGpuIndexBuffer() = delete;
    DawnGpuIndexBuffer(const DawnGpuIndexBuffer&) = delete;
    DawnGpuIndexBuffer& operator=(const DawnGpuIndexBuffer&) = delete;
    DawnGpuIndexBuffer(DawnGpuIndexBuffer&&) = default;
    DawnGpuIndexBuffer& operator=(DawnGpuIndexBuffer&&) = default;

    // wgpu::Buffer is ref counted so nothing to do here.
    ~DawnGpuIndexBuffer() override {};

    wgpu::Buffer GetBuffer() const { return m_Buffer; }

    Subrange GetSubrange(const uint32_t itemOffset, const uint32_t itemCount) override
    {
        eassert(itemOffset + itemCount <= m_ItemCount, "Sub-range out of bounds");
        return SdlSubrange(this, itemOffset, itemCount);
    }

private:
    friend class DawnGpuDevice;

    explicit DawnGpuIndexBuffer(wgpu::Buffer buffer, const uint32_t itemCount)
        : m_Buffer(buffer),
          m_ItemCount(itemCount)
    {
    }

    wgpu::Buffer m_Buffer;
    const uint32_t m_ItemCount;
};

class DawnGpuTexture : public GpuTexture
{
public:
    DawnGpuTexture() = delete;
    DawnGpuTexture(const DawnGpuTexture&) = delete;
    DawnGpuTexture& operator=(const DawnGpuTexture&) = delete;
    DawnGpuTexture(DawnGpuTexture&&) = default;
    DawnGpuTexture& operator=(DawnGpuTexture&&) = default;

    // wgpu::Texture is ref counted so nothing to do here.
    ~DawnGpuTexture() override {};

    wgpu::Texture GetTexture() const { return m_Texture; }
    wgpu::Sampler GetSampler() const { return m_Sampler; }

private:
    friend class DawnGpuDevice;

    DawnGpuTexture(wgpu::Texture texture, wgpu::Sampler sampler)
        : m_Texture(texture),
          m_Sampler(sampler)
    {
    }

    wgpu::Texture m_Texture;
    wgpu::Sampler m_Sampler;
};

class DawnGpuVertexShader : public GpuVertexShader
{
public:
    DawnGpuVertexShader() = delete;
    DawnGpuVertexShader(const DawnGpuVertexShader&) = delete;
    DawnGpuVertexShader& operator=(const DawnGpuVertexShader&) = delete;
    DawnGpuVertexShader(DawnGpuVertexShader&&) = default;
    DawnGpuVertexShader& operator=(DawnGpuVertexShader&&) = default;

    ~DawnGpuVertexShader() override {};

    wgpu::ShaderModule GetShader() const { return m_Shader; }

private:
    friend class DawnGpuDevice;

    explicit DawnGpuVertexShader(wgpu::ShaderModule shader)
        : m_Shader(shader)
    {
    }

    wgpu::ShaderModule m_Shader;
};

class DawnGpuFragmentShader : public GpuFragmentShader
{
public:
    DawnGpuFragmentShader() = delete;
    DawnGpuFragmentShader(const DawnGpuFragmentShader&) = delete;
    DawnGpuFragmentShader& operator=(const DawnGpuFragmentShader&) = delete;
    DawnGpuFragmentShader(DawnGpuFragmentShader&&) = default;
    DawnGpuFragmentShader& operator=(DawnGpuFragmentShader&&) = default;

    ~DawnGpuFragmentShader() override {};

    wgpu::ShaderModule GetShader() const { return m_Shader; }

private:
    friend class DawnGpuDevice;
    explicit DawnGpuFragmentShader(wgpu::ShaderModule shader)
        : m_Shader(shader)
    {
    }

    wgpu::ShaderModule m_Shader;
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
    Extent GetExtent() const override;

    Result<GpuVertexBuffer*> CreateVertexBuffer(const std::span<const Vertex>& vertices) override;

    Result<GpuVertexBuffer*> CreateVertexBuffer(
        const std::span<std::span<const Vertex>>& vertices) override;

    Result<void> DestroyVertexBuffer(GpuVertexBuffer* buffer) override;

    Result<GpuIndexBuffer*> CreateIndexBuffer(const std::span<const VertexIndex>& indices) override;

    Result<GpuIndexBuffer*> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) override;

    Result<void> DestroyIndexBuffer(GpuIndexBuffer* buffer) override;

    /// @brief Creates a texture from raw pixel data.
    /// Pixels are expected to be in RGBA8 format.
    /// rowStride is the number of bytes between the start of each row.
    /// rowStride must be at least width * 4.
    Result<GpuTexture*> CreateTexture(const unsigned width,
        const unsigned height,
        const uint8_t* pixels,
        const unsigned rowStride,
        const imstring& name) override;

    /// @brief Creates a 1x1 texture from a color.
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

    /// @brief Retrieves or creates a graphics pipeline for the given material.
    Result<wgpu::RenderPipeline*> GetOrCreatePipeline(const Material& mtl);

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
        wgpu::Surface surface);

    struct PipelineKey
    {
        const int ColorFormat;
        wgpu::ShaderModule const VertexShader;
        wgpu::ShaderModule const FragShader;

        bool operator<(const PipelineKey& other) const
        {
            return std::memcmp(this, &other, sizeof(*this)) < 0;
        }
    };

    std::map<PipelineKey, wgpu::RenderPipeline> m_PipelinesByKey;

    /// @brief Default sampler used for all textures.
    wgpu::Sampler m_Sampler = nullptr;

    union GpuResource
    {
        GpuResource() {}
        ~GpuResource() {}

        DawnGpuVertexBuffer VertexBuffer;
        DawnGpuIndexBuffer IndexBuffer;
        DawnGpuTexture Texture;
        DawnGpuVertexShader VertexShader;
        DawnGpuFragmentShader FragmentShader;
    };

    PoolAllocator<GpuResource, 256> m_ResourceAllocator;
};