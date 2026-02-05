#pragma once

#include "GpuDevice.h"
#include "Material.h"
#include "Vertex.h"

#include <map>
#include <span>
#include <webgpu/webgpu_cpp.h>

struct SDL_Window;

class DawnGpuVertexBuffer : public GpuVertexBuffer
{
public:
    DawnGpuVertexBuffer() = delete;
    DawnGpuVertexBuffer(const DawnGpuVertexBuffer&) = delete;
    DawnGpuVertexBuffer& operator=(const DawnGpuVertexBuffer&) = delete;

    // wgpu::Buffer is ref counted so nothing to do here.
    ~DawnGpuVertexBuffer() override {};

    wgpu::Buffer GetBuffer() const { return m_Buffer; }

private:
    friend class DawnGpuDevice;

    explicit DawnGpuVertexBuffer(wgpu::Buffer buffer)
        : m_Buffer(buffer)
    {
    }

    wgpu::Buffer m_Buffer;
};

class DawnGpuIndexBuffer : public GpuIndexBuffer
{
public:
    DawnGpuIndexBuffer() = delete;
    DawnGpuIndexBuffer(const DawnGpuIndexBuffer&) = delete;
    DawnGpuIndexBuffer& operator=(const DawnGpuIndexBuffer&) = delete;

    // wgpu::Buffer is ref counted so nothing to do here.
    ~DawnGpuIndexBuffer() override {};

    wgpu::Buffer GetBuffer() const { return m_Buffer; }

private:
    friend class DawnGpuDevice;

    explicit DawnGpuIndexBuffer(wgpu::Buffer buffer)
        : m_Buffer(buffer)
    {
    }

    wgpu::Buffer m_Buffer;
};

class DawnGpuTexture : public GpuTexture
{
public:
    DawnGpuTexture() = delete;
    DawnGpuTexture(const DawnGpuTexture&) = delete;
    DawnGpuTexture& operator=(const DawnGpuTexture&) = delete;

    ~DawnGpuTexture() override;

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

    ~DawnGpuVertexShader() override;

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

    ~DawnGpuFragmentShader() override;

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

    ~DawnGpuDevice() override;

    /// @brief Gets the renderable extent of the device.
    Extent GetExtent() const override;

    Result<VertexBuffer> CreateVertexBuffer(const std::span<const Vertex>& vertices) override;

    Result<VertexBuffer> CreateVertexBuffer(
        const std::span<std::span<const Vertex>>& vertices) override;

    Result<IndexBuffer> CreateIndexBuffer(const std::span<const VertexIndex>& indices) override;

    Result<IndexBuffer> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) override;

    /// @brief Creates a texture from raw pixel data.
    /// Pixels are expected to be in RGBA8 format.
    /// rowStride is the number of bytes between the start of each row.
    /// rowStride must be at least width * 4.
    Result<Texture> CreateTexture(const unsigned width,
        const unsigned height,
        const uint8_t* pixels,
        const unsigned rowStride,
        const imstring& name) override;

    /// @brief Creates a 1x1 texture from a color.
    Result<Texture> CreateTexture(const RgbaColorf& color, const imstring& name) override;

    /// @brief Destroys a texture.
    Result<void> DestroyTexture(Texture& texture) override;

    /// @brief Creates a vertex shader from the given specification.
    Result<VertexShader> CreateVertexShader(const VertexShaderSpec& shaderSpec) override;

    /// @brief Destroys a vertex shader.
    Result<void> DestroyVertexShader(VertexShader& shader) override;

    /// @brief Creates a fragment shader from the given specification.
    Result<FragmentShader> CreateFragmentShader(const FragmentShaderSpec& shaderSpec) override;

    /// @brief Destroys a fragment shader.
    Result<void> DestroyFragmentShader(FragmentShader& shader) override;

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

    Result<Texture> CreateTexture(
        const unsigned width, const unsigned height, const uint8_t* pixels);

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
};