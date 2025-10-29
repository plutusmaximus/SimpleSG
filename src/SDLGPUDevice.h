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

    std::expected<SDL_GPUTexture*, Error> GetOrCreateTexture(const std::string_view path);

    SDL_GPUTexture* GetTexture(const std::string_view path);

    SDL_GPUShader* GetVertexShader(const std::string_view path);

    SDL_GPUShader* GetFragShader(const std::string_view path);

    SDL_GPUSampler* m_Sampler = nullptr;

    struct PipelineKey
    {
        SDL_GPUTextureFormat const ColorFormat;
        SDL_GPUShader* const VertexShader;
        SDL_GPUShader* const FragShader;

        bool operator<(const PipelineKey& other) const
        {
            return std::memcmp(this, &other, sizeof(*this)) < 0;
        }
    };

    static inline constexpr std::hash<std::string_view> MakeHashKey;

    using HashKey = size_t;

    template<typename T>
    struct Record
    {
        const std::string Name;
        T* const Item;
    };

    template<typename T>
    class HashTable : public std::unordered_multimap<HashKey, Record<T>>
    {
    public:

        void Add(const std::string_view path, T* item)
        {
            const HashKey hashKey = MakeHashKey(path);

            this->emplace(hashKey, Record<T>{ .Name{path}, .Item = item });
        }

        T* Find(const std::string_view path)
        {
            const HashKey hashKey = MakeHashKey(path);

            auto range = this->equal_range(hashKey);

            for (auto it = range.first; it != range.second; ++it)
            {
                if (path == it->second.Name)
                {
                    return it->second.Item;
                }
            }

            return nullptr;
        }
    };

    std::map<PipelineKey, SDL_GPUGraphicsPipeline*> m_PipelinesByKey;
    HashTable<SDL_GPUTexture> m_TexturesByName;
    HashTable<SDL_GPUShader> m_VertexShadersByName;
    HashTable<SDL_GPUShader> m_FragShadersByName;
    std::vector<SDLMaterial*> m_Materials;
    std::map<MaterialId, size_t> m_MaterialIndexById;
};