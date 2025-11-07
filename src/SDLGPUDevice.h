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

class SDLGpuBuffer : public GpuBuffer
{
public:

    SDLGpuBuffer() = delete;

    ~SDLGpuBuffer();

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

    static Result<RefPtr<GPUDevice>> Create(SDL_Window* window);

    ~SDLGPUDevice() override;

    Result<RefPtr<ModelNode>> CreateModel(const ModelSpec& modelSpec) override;

    Result<RefPtr<RenderGraph>> CreateRenderGraph() override;

    Result<const SDLMaterial*> GetMaterial(const MaterialId& mtlId) const;

    Result<SDL_GPUShader*> GetOrCreateVertexShader(
        const std::string_view path,
        const int numUniformBuffers);

    Result<SDL_GPUShader*> GetOrCreateFragmentShader(
        const std::string_view path,
        const int numSamplers);

    Result<SDL_GPUGraphicsPipeline*> GetOrCreatePipeline(const SDLMaterial& mtl);

    SDL_Window* const m_Window;
    SDL_GPUDevice* const m_GpuDevice;

private:

    SDLGPUDevice() = delete;

    SDLGPUDevice(SDL_Window* window, SDL_GPUDevice* gpuDevice);

    Result<std::tuple<VertexBuffer, IndexBuffer>> CreateBuffers(
        const std::span<const Vertex>& vertices,
        const std::span<const VertexIndex>& indices);

    Result<SDL_GPUTexture*> GetOrCreateTexture(const std::string_view path);

    SDL_GPUTexture* GetTexture(const std::string_view path);

    SDL_GPUShader* GetVertexShader(const std::string_view path);

    SDL_GPUShader* GetFragShader(const std::string_view path);

    SDL_GPUSampler* m_Sampler = nullptr;

    struct PipelineKey
    {
        int const ColorFormat;
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