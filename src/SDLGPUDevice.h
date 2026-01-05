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

class SDLGpuBuffer : public GpuBuffer
{
public:

    SDLGpuBuffer() = delete;

    ~SDLGpuBuffer() override;

    SDL_GPUBuffer* const Buffer;

private:

    friend class SDLGPUDevice;

    SDLGpuBuffer(SDL_GPUDevice* gpuDevice, SDL_GPUBuffer* buffer)
        : m_GpuDevice(gpuDevice)
        , Buffer(buffer)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
};

class SDLGpuTexture : public GpuTexture
{
public:

    SDLGpuTexture() = delete;

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

class SDLGPUDevice : public GPUDevice
{
public:

    static Result<RefPtr<SDLGPUDevice>> Create(SDL_Window* window);

    ~SDLGPUDevice() override;

    /// @brief Gets the renderable extent of the device.
    Extent GetExtent() const override;

    Result<GpuIndexBuffer> CreateIndexBuffer(
        const std::span<const VertexIndex>& indices) override;

    Result<GpuVertexBuffer> CreateVertexBuffer(
        const std::span<const Vertex>& vertices) override;

    Result<GpuIndexBuffer> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) override;

    Result<GpuVertexBuffer> CreateVertexBuffer(
        const std::span<std::span<const Vertex>>& vertices) override;

    Result<RefPtr<GpuTexture>> CreateTexture(const TextureSpec& textureSpec) override;

    Result<RefPtr<GpuVertexShader>> CreateVertexShader(const VertexShaderSpec& shaderSpec) override;

    Result<RefPtr<GpuFragmentShader>> CreateFragmentShader(const FragmentShaderSpec& shaderSpec) override;

    /// @brief Retrieves or creates a graphics pipeline for the given material.
    Result<SDL_GPUGraphicsPipeline*> GetOrCreatePipeline(const Material& mtl);

    SDL_Window* const Window;
    SDL_GPUDevice* const Device;

private:

    SDLGPUDevice() = delete;

    SDLGPUDevice(SDL_Window* window, SDL_GPUDevice* gpuDevice);

    /// @brief Creates a texture from an image.
    Result<RefPtr<GpuTexture>> CreateTexture(const RefPtr<Image> image);

    /// @brief Creates a 1x1 texture from a color.
    Result<RefPtr<GpuTexture>> CreateTexture(const RgbaColorf& color);

    Result<RefPtr<GpuTexture>> CreateTexture(const unsigned width, const unsigned height, const uint8_t* pixels);

    /// @brief Creates a texture from a file path.
    Result<RefPtr<GpuTexture>> CreateTexture(const std::string_view path);
    
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
};