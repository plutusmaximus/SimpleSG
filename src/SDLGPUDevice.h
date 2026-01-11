#pragma once

#include "GPUDevice.h"
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
class Image;

class SDLGpuVertexBuffer : public GpuVertexBuffer
{
public:

    SDLGpuVertexBuffer() = delete;
    SDLGpuVertexBuffer(const SDLGpuVertexBuffer&) = delete;
    SDLGpuVertexBuffer& operator=(const SDLGpuVertexBuffer&) = delete;

    ~SDLGpuVertexBuffer() override;

    SDL_GPUBuffer* const Buffer;

private:

    friend class SDLGPUDevice;

    SDLGpuVertexBuffer(SDL_GPUDevice* gpuDevice, SDL_GPUBuffer* buffer)
        : Buffer(buffer)
        , m_GpuDevice(gpuDevice)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
};

class SDLGpuIndexBuffer : public GpuIndexBuffer
{
public:

    SDLGpuIndexBuffer() = delete;
    SDLGpuIndexBuffer(const SDLGpuIndexBuffer&) = delete;
    SDLGpuIndexBuffer& operator=(const SDLGpuIndexBuffer&) = delete;

    ~SDLGpuIndexBuffer() override;

    SDL_GPUBuffer* const Buffer;

private:

    friend class SDLGPUDevice;

    SDLGpuIndexBuffer(SDL_GPUDevice* gpuDevice, SDL_GPUBuffer* buffer)
        : Buffer(buffer)
        , m_GpuDevice(gpuDevice)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
};

class SDLGpuTexture : public GpuTexture
{
public:

    SDLGpuTexture() = delete;
    SDLGpuTexture(const SDLGpuTexture&) = delete;
    SDLGpuTexture& operator=(const SDLGpuTexture&) = delete;

    ~SDLGpuTexture() override;

    SDL_GPUTexture* const Texture;
    SDL_GPUSampler* const Sampler;

private:

    friend class SDLGPUDevice;

    SDLGpuTexture(SDL_GPUDevice* gpuDevice, SDL_GPUTexture* texture, SDL_GPUSampler* sampler)
        : m_GpuDevice(gpuDevice)
        , Texture(texture)
        , Sampler(sampler)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
};

class SDLGpuVertexShader : public GpuVertexShader
{
public:

    SDLGpuVertexShader() = delete;
    SDLGpuVertexShader(const SDLGpuVertexShader&) = delete;
    SDLGpuVertexShader& operator=(const SDLGpuVertexShader&) = delete;
    
    ~SDLGpuVertexShader() override;

    SDL_GPUShader* const Shader;

private:

    friend class SDLGPUDevice;

    SDLGpuVertexShader(SDL_GPUDevice* gpuDevice, SDL_GPUShader* shader)
        : m_GpuDevice(gpuDevice)
        , Shader(shader)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
};

class SDLGpuFragmentShader : public GpuFragmentShader
{
public:

    SDLGpuFragmentShader() = delete;
    SDLGpuFragmentShader(const SDLGpuFragmentShader&) = delete;
    SDLGpuFragmentShader& operator=(const SDLGpuFragmentShader&) = delete;
    
    ~SDLGpuFragmentShader() override;

    SDL_GPUShader* const Shader;

private:

    friend class SDLGPUDevice;

    SDLGpuFragmentShader(SDL_GPUDevice* gpuDevice, SDL_GPUShader* shader)
        : m_GpuDevice(gpuDevice)
        , Shader(shader)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
};

/// @brief SDL GPU Device implementation.
class SDLGPUDevice : public GPUDevice
{
public:

    static Result<GPUDevice*> Create(SDL_Window* window);

    static void Destroy(GPUDevice* device);

    SDLGPUDevice() = delete;
    SDLGPUDevice(const SDLGPUDevice&) = delete;
    SDLGPUDevice& operator=(const SDLGPUDevice&) = delete;

    ~SDLGPUDevice() override;

    /// @brief Gets the renderable extent of the device.
    Extent GetExtent() const override;

    Result<VertexBuffer> CreateVertexBuffer(
        const std::span<const Vertex>& vertices) override;

    Result<VertexBuffer> CreateVertexBuffer(
        const std::span<std::span<const Vertex>>& vertices) override;

    Result<IndexBuffer> CreateIndexBuffer(
        const std::span<const VertexIndex>& indices) override;

    Result<IndexBuffer> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) override;

    /// @brief Creates a texture from an image.
    Result<Texture> CreateTexture(const Image& image) override;

    /// @brief Creates a 1x1 texture from a color.
    Result<Texture> CreateTexture(const RgbaColorf& color) override;

    Result<VertexShader> CreateVertexShader(const VertexShaderSpec& shaderSpec) override;

    Result<FragmentShader> CreateFragmentShader(const FragmentShaderSpec& shaderSpec) override;

    Result<RenderGraph*> CreateRenderGraph() override;

    void DestroyRenderGraph(RenderGraph* renderGraph) override;

    /// @brief Retrieves or creates a graphics pipeline for the given material.
    Result<SDL_GPUGraphicsPipeline*> GetOrCreatePipeline(const Material& mtl);

    SDL_Window* const Window;
    SDL_GPUDevice* const Device;

private:

    SDLGPUDevice(SDL_Window* window, SDL_GPUDevice* gpuDevice);

    Result<Texture> CreateTexture(const unsigned width, const unsigned height, const uint8_t* pixels);
    
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