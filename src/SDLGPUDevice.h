#pragma once

#include "GPUDevice.h"
#include "Material.h"

#include <map>

#include <SDL3/SDL_gpu.h>

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

    IMPLEMENT_NON_COPYABLE(SDLVertexBuffer);
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

    IMPLEMENT_NON_COPYABLE(SDLIndexBuffer);
};

class SDLMaterial
{
public:

    SDLMaterial() = delete;

    const MaterialId Id;

    const RgbaColorf Color;

    const float Metallic{ 0 };
    const float Roughness{ 0 };

    SDL_GPUTexture* const Albedo = nullptr;
    SDL_GPUSampler* const AlbedoSampler = nullptr;
    SDL_GPUShader* const VertexShader = nullptr;
    SDL_GPUShader* const FragmentShader = nullptr;;

private:

    friend class SDLGPUDevice;

    SDLMaterial(
        const RgbaColorf& color,
        SDL_GPUTexture* albedo,
        SDL_GPUSampler* albedoSampler,
        SDL_GPUShader* vertexShader,
        SDL_GPUShader* fragmentShader)
        : Id(MaterialId::NextId())
        , Color(color)
        , Albedo(albedo)
        , AlbedoSampler(albedoSampler)
        , VertexShader(vertexShader)
        , FragmentShader(fragmentShader)
    {
    }

    IMPLEMENT_NON_COPYABLE(SDLMaterial);
};

class SDLGPUDevice : public GPUDevice
{
public:

    static std::expected<RefPtr<GPUDevice>, Error> Create(SDL_Window* window);

    ~SDLGPUDevice() override;

    std::expected<RefPtr<Model>, Error> CreateModel(const ModelSpec& modelSpec) override;

    std::expected<RefPtr<RenderGraph>, Error> CreateRenderGraph() override;

    std::expected<const SDLMaterial*, Error> GetMaterial(const MaterialId& mtlId) const;

    std::expected<SDL_GPUShader*, Error> GetOrCreateVertexShader(
        const std::string_view path,
        const int numUniformBuffers);

    std::expected<SDL_GPUShader*, Error> GetOrCreateFragmentShader(
        const std::string_view path,
        const int numSamplers);

    std::expected<SDL_GPUGraphicsPipeline*, Error> GetOrCreatePipeline(const SDLMaterial& mtl);

    SDL_Window* const m_Window;
    SDL_GPUDevice* const m_GpuDevice;

private:

    SDLGPUDevice() = delete;

    SDLGPUDevice(SDL_Window* window, SDL_GPUDevice* gpuDevice);

    std::expected<VertexBuffer*, Error> CreateVertexBuffer(const std::span<Vertex>& vertices);

    std::expected<IndexBuffer*, Error> CreateIndexBuffer(const std::span<VertexIndex>& indices);

    std::expected<SDL_GPUTexture*, Error> GetOrCreateTextureFromPNG(const std::string_view path);

    SDL_GPUTexture* GetTexture(const std::string_view path);

    SDL_GPUShader* GetVertexShader(const std::string_view path);

    SDL_GPUShader* GetFragShader(const std::string_view path);

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

    struct PipelineKey
    {
        SDL_GPUTextureFormat const ColorFormat;
        SDL_GPUShader* const VertexShader;
        SDL_GPUShader* const FragShader;

        bool operator<(const PipelineKey& other) const
        {
            return ColorFormat < other.ColorFormat
                || VertexShader < other.VertexShader
                || FragShader < other.FragShader;
        }
    };

    std::map<PipelineKey, SDL_GPUGraphicsPipeline*> m_PipelinesByKey;
    std::map<size_t, TextureRecord> m_TexturesByName;
    std::map<size_t, ShaderRecord> m_VertexShadersByName;
    std::map<size_t, ShaderRecord> m_FragShadersByName;
    std::vector<SDLMaterial*> m_Materials;
    std::map<MaterialId, size_t> m_MaterialIndexById;
};