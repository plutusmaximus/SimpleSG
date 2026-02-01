#pragma once

#include "GpuDevice.h"
#include "Material.h"
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
public:

    SdlGpuVertexBuffer() = delete;
    SdlGpuVertexBuffer(const SdlGpuVertexBuffer&) = delete;
    SdlGpuVertexBuffer& operator=(const SdlGpuVertexBuffer&) = delete;

    ~SdlGpuVertexBuffer() override;

    SDL_GPUBuffer* GetBuffer() const { return m_Buffer; }

private:

    friend class SdlGpuDevice;

    SdlGpuVertexBuffer(SDL_GPUDevice* gpuDevice, SDL_GPUBuffer* buffer)
        : m_Buffer(buffer)
        , m_GpuDevice(gpuDevice)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
    SDL_GPUBuffer* const m_Buffer;
};

class SdlGpuIndexBuffer : public GpuIndexBuffer
{
public:

    SdlGpuIndexBuffer() = delete;
    SdlGpuIndexBuffer(const SdlGpuIndexBuffer&) = delete;
    SdlGpuIndexBuffer& operator=(const SdlGpuIndexBuffer&) = delete;

    ~SdlGpuIndexBuffer() override;

    SDL_GPUBuffer* GetBuffer() const { return m_Buffer; }

private:

    friend class SdlGpuDevice;

    SdlGpuIndexBuffer(SDL_GPUDevice* gpuDevice, SDL_GPUBuffer* buffer)
        : m_Buffer(buffer)
        , m_GpuDevice(gpuDevice)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
    SDL_GPUBuffer* const m_Buffer;
};

class SdlGpuTexture : public GpuTexture
{
public:

    SdlGpuTexture() = delete;
    SdlGpuTexture(const SdlGpuTexture&) = delete;
    SdlGpuTexture& operator=(const SdlGpuTexture&) = delete;

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

    ~SdlGpuDevice() override;

    /// @brief Gets the renderable extent of the device.
    Extent GetExtent() const override;

    Result<VertexBuffer> CreateVertexBuffer(
        const std::span<const Vertex>& vertices) override;

    Result<VertexBuffer> CreateVertexBuffer(
        const std::span<std::span<const Vertex>>& vertices) override;

    /// @brief Destroys a vertex buffer.
    Result<void> DestroyVertexBuffer(VertexBuffer& buffer) override;

    Result<IndexBuffer> CreateIndexBuffer(
        const std::span<const VertexIndex>& indices) override;

    Result<IndexBuffer> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) override;

    /// @brief Destroys an index buffer.
    Result<void> DestroyIndexBuffer(IndexBuffer& buffer) override;

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
    virtual Result<void> DestroyTexture(Texture& texture) override;

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
};