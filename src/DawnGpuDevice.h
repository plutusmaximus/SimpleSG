#pragma once

#include "GpuDevice.h"
#include "PoolAllocator.h"
#include "Vertex.h"

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
    DawnGpuVertexBuffer(DawnGpuVertexBuffer&&) = delete;
    DawnGpuVertexBuffer& operator=(DawnGpuVertexBuffer&&) = delete;

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

    explicit DawnGpuVertexBuffer(
        DawnGpuDevice* gpuDevice, wgpu::Buffer buffer, const uint32_t itemCount)
        : m_GpuDevice(gpuDevice),
          m_Buffer(buffer),
          m_ItemCount(itemCount)
    {
    }

    DawnGpuDevice* m_GpuDevice;
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
    DawnGpuIndexBuffer(DawnGpuIndexBuffer&&) = delete;
    DawnGpuIndexBuffer& operator=(DawnGpuIndexBuffer&&) = delete;

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

    explicit DawnGpuIndexBuffer(
        DawnGpuDevice* gpuDevice, wgpu::Buffer buffer, const uint32_t itemCount)
        : m_GpuDevice(gpuDevice),
          m_Buffer(buffer),
          m_ItemCount(itemCount)
    {
    }

    DawnGpuDevice* m_GpuDevice;
    wgpu::Buffer m_Buffer;
    const uint32_t m_ItemCount;
};

class DawnGpuTexture : public GpuTexture
{
public:
    DawnGpuTexture() = delete;
    DawnGpuTexture(const DawnGpuTexture&) = delete;
    DawnGpuTexture& operator=(const DawnGpuTexture&) = delete;
    DawnGpuTexture(DawnGpuTexture&&) = delete;
    DawnGpuTexture& operator=(DawnGpuTexture&&) = delete;

    // wgpu::Texture is ref counted so nothing to do here.
    ~DawnGpuTexture() override {};

    unsigned GetWidth() const override { return m_Width; }
    unsigned GetHeight() const override { return m_Height; }

    wgpu::Texture GetTexture() const { return m_Texture; }
    wgpu::Sampler GetSampler() const { return m_Sampler; }

private:
    friend class DawnGpuDevice;

    DawnGpuTexture(DawnGpuDevice* gpuDevice, wgpu::Texture texture, wgpu::Sampler sampler, const unsigned width, const unsigned height)
        : m_GpuDevice(gpuDevice),
          m_Texture(texture),
          m_Sampler(sampler),
          m_Width(width),
          m_Height(height)
    {
    }

    DawnGpuDevice* m_GpuDevice;
    wgpu::Texture m_Texture;
    wgpu::Sampler m_Sampler;
    unsigned m_Width;
    unsigned m_Height;
};

class DawnGpuColorTarget : public GpuColorTarget
{
public:
    DawnGpuColorTarget() = delete;
    DawnGpuColorTarget(const DawnGpuColorTarget&) = delete;
    DawnGpuColorTarget& operator=(const DawnGpuColorTarget&) = delete;
    DawnGpuColorTarget(DawnGpuColorTarget&&) = delete;
    DawnGpuColorTarget& operator=(DawnGpuColorTarget&&) = delete;

    // wgpu::Texture is ref counted so nothing to do here.
    ~DawnGpuColorTarget() override {};

    unsigned GetWidth() const override { return m_Width; }
    unsigned GetHeight() const override { return m_Height; }

    wgpu::Texture GetTexture() const { return m_Texture; }
    wgpu::TextureView GetTextureView() const { return m_TextureView; }
    wgpu::Sampler GetSampler() const { return m_Sampler; }

private:
    friend class DawnGpuDevice;

    DawnGpuColorTarget(DawnGpuDevice* gpuDevice,
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

    DawnGpuDevice* m_GpuDevice;
    wgpu::Texture m_Texture;
    wgpu::TextureView m_TextureView;
    wgpu::Sampler m_Sampler;
    unsigned m_Width;
    unsigned m_Height;
};

class DawnGpuDepthTarget : public GpuDepthTarget
{
public:
    DawnGpuDepthTarget() = delete;
    DawnGpuDepthTarget(const DawnGpuDepthTarget&) = delete;
    DawnGpuDepthTarget& operator=(const DawnGpuDepthTarget&) = delete;
    DawnGpuDepthTarget(DawnGpuDepthTarget&&) = delete;
    DawnGpuDepthTarget& operator=(DawnGpuDepthTarget&&) = delete;

    // wgpu::Texture is ref counted so nothing to do here.
    ~DawnGpuDepthTarget() override {};

    unsigned GetWidth() const override { return m_Width; }
    unsigned GetHeight() const override { return m_Height; }

    wgpu::Texture GetDepthTarget() const { return m_DepthTarget; }
    wgpu::TextureView GetDepthTargetView() const { return m_DepthTargetView; }

private:
    friend class DawnGpuDevice;

    DawnGpuDepthTarget(DawnGpuDevice* gpuDevice,
        wgpu::Texture depthTarget,
        wgpu::TextureView depthTargetView,
        const unsigned width,
        const unsigned height)
        : m_GpuDevice(gpuDevice),
          m_DepthTarget(depthTarget),
          m_DepthTargetView(depthTargetView),
          m_Width(width),
          m_Height(height)
    {
    }

    DawnGpuDevice* m_GpuDevice;
    wgpu::Texture m_DepthTarget;
    wgpu::TextureView m_DepthTargetView;
    unsigned m_Width;
    unsigned m_Height;
};

class DawnGpuVertexShader : public GpuVertexShader
{
public:
    DawnGpuVertexShader() = delete;
    DawnGpuVertexShader(const DawnGpuVertexShader&) = delete;
    DawnGpuVertexShader& operator=(const DawnGpuVertexShader&) = delete;
    DawnGpuVertexShader(DawnGpuVertexShader&&) = delete;
    DawnGpuVertexShader& operator=(DawnGpuVertexShader&&) = delete;

    ~DawnGpuVertexShader() override {};

    wgpu::ShaderModule GetShader() const { return m_Shader; }

private:
    friend class DawnGpuDevice;

    explicit DawnGpuVertexShader(DawnGpuDevice* gpuDevice, wgpu::ShaderModule shader)
        : m_GpuDevice(gpuDevice),
          m_Shader(shader)
    {
    }

    DawnGpuDevice* m_GpuDevice;
    wgpu::ShaderModule m_Shader;
};

class DawnGpuFragmentShader : public GpuFragmentShader
{
public:
    DawnGpuFragmentShader() = delete;
    DawnGpuFragmentShader(const DawnGpuFragmentShader&) = delete;
    DawnGpuFragmentShader& operator=(const DawnGpuFragmentShader&) = delete;
    DawnGpuFragmentShader(DawnGpuFragmentShader&&) = delete;
    DawnGpuFragmentShader& operator=(DawnGpuFragmentShader&&) = delete;

    ~DawnGpuFragmentShader() override {};

    wgpu::ShaderModule GetShader() const { return m_Shader; }

private:
    friend class DawnGpuDevice;
    explicit DawnGpuFragmentShader(DawnGpuDevice* gpuDevice, wgpu::ShaderModule shader)
        : m_GpuDevice(gpuDevice),
          m_Shader(shader)
    {
    }

    DawnGpuDevice* m_GpuDevice;
    wgpu::ShaderModule m_Shader;
};

class DawnGpuPipeline : public GpuPipeline
{
public:

    DawnGpuPipeline() = delete;
    DawnGpuPipeline(const DawnGpuPipeline&) = delete;
    DawnGpuPipeline& operator=(const DawnGpuPipeline&) = delete;
    DawnGpuPipeline(DawnGpuPipeline&&) = delete;
    DawnGpuPipeline& operator=(DawnGpuPipeline&&) = delete;

    ~DawnGpuPipeline() override;

    wgpu::RenderPipeline* GetPipeline() const { return m_Pipeline; }

private:
    friend class DawnGpuDevice;

    DawnGpuPipeline(DawnGpuDevice* gpuDevice,
        wgpu::RenderPipeline* pipeline)
        : m_GpuDevice(gpuDevice),
          m_Pipeline(pipeline)
    {
    }

    DawnGpuDevice* const m_GpuDevice;
    wgpu::RenderPipeline* const m_Pipeline;
};

class DawnGpuRenderPass : public GpuRenderPass
{
public:

    DawnGpuRenderPass() = delete;
    DawnGpuRenderPass(const DawnGpuRenderPass&) = delete;
    DawnGpuRenderPass& operator=(const DawnGpuRenderPass&) = delete;
    DawnGpuRenderPass(DawnGpuRenderPass&&) = delete;
    DawnGpuRenderPass& operator=(DawnGpuRenderPass&&) = delete;

    ~DawnGpuRenderPass() override;

    wgpu::RenderPassEncoder* GetRenderPass() const { return m_RenderPass; }

private:

    friend class DawnGpuDevice;

    DawnGpuRenderPass(DawnGpuDevice* gpuDevice, wgpu::RenderPassEncoder* renderPass)
        : m_GpuDevice(gpuDevice)
        , m_RenderPass(renderPass)
    {
    }

    DawnGpuDevice* const m_GpuDevice;
    wgpu::RenderPassEncoder* const m_RenderPass;
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

    Result<GpuVertexShader*> CreateVertexShader(const std::span<const uint8_t>& shaderCode) override;

    Result<void> DestroyVertexShader(GpuVertexShader* shader) override;

    Result<GpuFragmentShader*> CreateFragmentShader(const std::span<const uint8_t>& shaderCode) override;

    Result<void> DestroyFragmentShader(GpuFragmentShader* shader) override;

    Result<GpuPipeline*> CreatePipeline(const GpuPipelineType pipelineType,
        GpuVertexShader* vertexShader,
        GpuFragmentShader* fragmentShader) override;

    Result<void> DestroyPipeline(GpuPipeline* pipeline) override;

    Result<Renderer*> CreateRenderer(GpuPipeline* pipeline) override;

    void DestroyRenderer(Renderer* renderer) override;

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

    Result<wgpu::Sampler> GetDefaultSampler();

    /// @brief Default sampler used for all textures.
    wgpu::Sampler m_Sampler = nullptr;

    union GpuResource
    {
        GpuResource() {}
        ~GpuResource() {}

        DawnGpuVertexBuffer VertexBuffer;
        DawnGpuIndexBuffer IndexBuffer;
        DawnGpuTexture Texture;
        DawnGpuColorTarget ColorTarget;
        DawnGpuDepthTarget DepthTarget;
        DawnGpuVertexShader VertexShader;
        DawnGpuFragmentShader FragmentShader;
        DawnGpuPipeline Pipeline;
        DawnGpuRenderPass RenderPass;
    };

    PoolAllocator<GpuResource, 256> m_ResourceAllocator;
};