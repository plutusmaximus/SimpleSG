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

    ~SDLGpuVertexBuffer() override;

    /// @brief Retrieves a sub-range vertex buffer from this buffer.
    Result<RefPtr<GpuVertexBuffer>> GetSubRange(
        const uint32_t itemOffset,
        const uint32_t itemCount) override;

    SDL_GPUBuffer* const Buffer;

private:

    friend class SDLGPUDevice;

    SDLGpuVertexBuffer(SDL_GPUDevice* gpuDevice, SDL_GPUBuffer* buffer, const uint32_t itemOffset, const uint32_t itemCount)
        : GpuVertexBuffer(itemOffset, itemCount)
        , Buffer(buffer)
        , m_GpuDevice(gpuDevice)
    {
    }

    SDLGpuVertexBuffer(RefPtr<GpuVertexBuffer> baseBuffer, const uint32_t itemOffset, const uint32_t itemCount)
        : GpuVertexBuffer(itemOffset, itemCount)
        , Buffer(baseBuffer.Get<SDLGpuVertexBuffer>()->Buffer)
        , m_GpuDevice(baseBuffer.Get<SDLGpuVertexBuffer>()->m_GpuDevice)
        , m_BaseBuffer(baseBuffer)
    {
        eassert(itemOffset + itemCount <= baseBuffer->ItemCount);
    }

    SDL_GPUDevice* const m_GpuDevice;

    /// @brief Base buffer from which this sub-range was created (if any).
    RefPtr<GpuVertexBuffer> m_BaseBuffer;
};

class SDLGpuIndexBuffer : public GpuIndexBuffer
{
public:

    SDLGpuIndexBuffer() = delete;

    ~SDLGpuIndexBuffer() override;

    /// @brief Retrieves a sub-range index buffer from this buffer.
    Result<RefPtr<GpuIndexBuffer>> GetSubRange(
        const uint32_t itemOffset,
        const uint32_t itemCount) override;

    SDL_GPUBuffer* const Buffer;

private:

    friend class SDLGPUDevice;

    SDLGpuIndexBuffer(SDL_GPUDevice* gpuDevice, SDL_GPUBuffer* buffer, const uint32_t itemOffset, const uint32_t itemCount)
        : GpuIndexBuffer(itemOffset, itemCount)
        , Buffer(buffer)
        , m_GpuDevice(gpuDevice)
    {
    }

    SDLGpuIndexBuffer(RefPtr<GpuIndexBuffer> baseBuffer, const uint32_t itemOffset, const uint32_t itemCount)
        : GpuIndexBuffer(itemOffset, itemCount)
        , Buffer(baseBuffer.Get<SDLGpuIndexBuffer>()->Buffer)
        , m_GpuDevice(baseBuffer.Get<SDLGpuIndexBuffer>()->m_GpuDevice)
        , m_BaseBuffer(baseBuffer)
    {
        eassert(itemOffset + itemCount <= baseBuffer->ItemCount);
    }

    SDL_GPUDevice* const m_GpuDevice;

    /// @brief Base buffer from which this sub-range was created (if any).
    RefPtr<GpuIndexBuffer> m_BaseBuffer;
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

    Result<RefPtr<GpuIndexBuffer>> CreateIndexBuffer(
        const std::span<const VertexIndex>& indices) override;

    Result<RefPtr<GpuVertexBuffer>> CreateVertexBuffer(
        const std::span<const Vertex>& vertices) override;

    Result<RefPtr<GpuIndexBuffer>> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) override;

    Result<RefPtr<GpuVertexBuffer>> CreateVertexBuffer(
        const std::span<std::span<const Vertex>>& vertices) override;

    /// @brief Creates a texture from an image.
    Result<Texture> CreateTexture(const Image& image) override;

    /// @brief Creates a 1x1 texture from a color.
    Result<Texture> CreateTexture(const RgbaColorf& color) override;

    Result<RefPtr<GpuVertexShader>> CreateVertexShader(const VertexShaderSpec& shaderSpec) override;

    Result<RefPtr<GpuFragmentShader>> CreateFragmentShader(const FragmentShaderSpec& shaderSpec) override;

    /// @brief Retrieves or creates a graphics pipeline for the given material.
    Result<SDL_GPUGraphicsPipeline*> GetOrCreatePipeline(const Material& mtl);

    SDL_Window* const Window;
    SDL_GPUDevice* const Device;

private:

    SDLGPUDevice() = delete;

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