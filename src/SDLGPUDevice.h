#pragma once

#include "GPUDevice.h"
#include "Material.h"

#include <map>

struct SDL_GPUDevice;
struct SDL_Window;
struct SDL_GPUBuffer;
struct SDL_GPUTexture;
struct SDL_GPUSampler;
struct SDL_GPUShader;

class SDLVertexBuffer : public VertexBuffer
{
public:

    SDLVertexBuffer() = delete;

    ~SDLVertexBuffer() override;

    SDL_GPUBuffer* const m_Buffer;

private:

    friend class SDLGPUDevice;

    SDLVertexBuffer(SDL_GPUDevice* gpuDevice, SDL_GPUBuffer* buffer)
        : m_GpuDevice(gpuDevice)
        , m_Buffer(buffer)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
};

class SDLIndexBuffer : public IndexBuffer
{
public:

    SDLIndexBuffer() = delete;

    ~SDLIndexBuffer() override;

    SDL_GPUBuffer* const m_Buffer;

private:

    friend class SDLGPUDevice;

    SDLIndexBuffer(SDL_GPUDevice* gpuDevice, SDL_GPUBuffer* buffer)
        : m_GpuDevice(gpuDevice)
        , m_Buffer(buffer)
    {
    }

    SDL_GPUDevice* const m_GpuDevice;
};

class SDLMaterial
{
public:

    const MaterialId Id;

    const RgbaColorf Color;

    const float Metallic{ 0 };
    const float Roughness{ 0 };

    SDL_GPUTexture* const Albedo;
    SDL_GPUSampler* const AlbedoSampler;

private:

    friend class SDLGPUDevice;

    SDLMaterial(const RgbaColorf& color, SDL_GPUTexture* albedo, SDL_GPUSampler* albedoSampler)
        : Id(MaterialId::NextId())
        , Color(color)
        , Albedo(albedo)
        , AlbedoSampler(albedoSampler)
    {
    }
};

class SDLGPUDevice : public GPUDevice
{
public:

    static std::expected<RefPtr<GPUDevice>, Error> Create(SDL_Window* window);

    ~SDLGPUDevice() override;

    std::expected<RefPtr<Model>, Error> CreateModel(const ModelSpec& modelSpec) override;

    std::expected<RefPtr<RenderGraph>, Error> CreateRenderGraph() override;

    std::expected<const SDLMaterial*, Error> GetMaterial(const MaterialId& mtlId) const;

    std::expected<SDL_GPUShader*, Error> GetOrLoadVertexShader(
        const std::string_view fileName,
        const int numUniformBuffers);

    std::expected<SDL_GPUShader*, Error> GetOrLoadFragmentShader(
        const std::string_view fileName,
        const int numSamplers);

    SDL_Window* const m_Window;
    SDL_GPUDevice* const m_GpuDevice;

private:

    SDLGPUDevice() = delete;

    SDLGPUDevice(SDL_Window* window, SDL_GPUDevice* gpuDevice);

    std::expected<VertexBuffer*, Error> CreateVertexBuffer(const std::span<Vertex>& vertices);

    std::expected<IndexBuffer*, Error> CreateIndexBuffer(const std::span<VertexIndex>& indices);

    std::expected<SDL_GPUTexture*, Error> GetOrLoadTextureFromPNG(const std::string_view path);

    SDL_GPUTexture* GetTexture(const std::string_view fileName);

    SDL_GPUShader* GetShader(const std::string_view fileName);

    SDL_GPUSampler* m_Sampler = nullptr;

    struct TextureRecord
    {
        const std::string Name;
        SDL_GPUTexture* const Texture;
    };

    struct ShaderRecord
    {
        const std::string Name;
        SDL_GPUShader* const Shader;
    };

    std::map<size_t, TextureRecord> m_TexturesByName;
    std::map<size_t, ShaderRecord> m_ShadersByName;
    std::vector<SDLMaterial*> m_Materials;
    std::map<MaterialId, int> m_MaterialIndexById;
};