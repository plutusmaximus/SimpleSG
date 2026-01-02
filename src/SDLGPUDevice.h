#pragma once

#include "GPUDevice.h"
#include "Material.h"
#include "Vertex.h"

#include <map>
#include <unordered_map>
#include <deque>
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

    /// @brief Unique key identifying this material.
    /// Used to group meshes sharing the same material attributes.
    const MaterialKey Key;

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
        : Key(MaterialId::NextId(), color.a < 1.0f ? MaterialFlags::Translucent : MaterialFlags::None)
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

    static Result<RefPtr<SDLGPUDevice>> Create(SDL_Window* window);

    ~SDLGPUDevice() override;

    /// @brief Creates a model from the given specification.
    Result<RefPtr<Model>> CreateModel(const ModelSpec& modelSpec) override;

    /// @brief Gets the renderable extent of the device.
    Extent GetExtent() const override;

    /// @brief Retrieves a material by its ID.
    Result<const SDLMaterial*> GetMaterial(const MaterialId& mtlId) const;

    /// @brief Retrieves or creates a vertex shader from a file path.
    Result<SDL_GPUShader*> GetOrCreateVertexShader(
        const std::string_view path,
        const int numUniformBuffers);

    /// @brief Retrieves or creates a fragment shader from a file path.
    Result<SDL_GPUShader*> GetOrCreateFragmentShader(
        const std::string_view path,
        const int numSamplers);

    /// @brief Retrieves or creates a graphics pipeline for the given material.
    Result<SDL_GPUGraphicsPipeline*> GetOrCreatePipeline(const SDLMaterial& mtl);

    SDL_Window* const Window;
    SDL_GPUDevice* const Device;

private:

    SDLGPUDevice() = delete;

    SDLGPUDevice(SDL_Window* window, SDL_GPUDevice* gpuDevice);

    Result<std::tuple<VertexBuffer, IndexBuffer>> CreateBuffers(
        const std::span<std::span<const Vertex>>& vertices,
        const std::span<std::span<const VertexIndex>>& indices);

    /// @brief Retrieves or creates a texture from a file path.
    Result<SDL_GPUTexture*> GetOrCreateTexture(const std::string_view path);

    /// @brief Retrieves or creates a texture from an image.
    Result<SDL_GPUTexture*> GetOrCreateTexture(const std::string_view key, const Image& image);

    /// @brief Retrieves a texture by its key.
    SDL_GPUTexture* GetTexture(const std::string_view key);

    /// @brief Retrieves a vertex shader by its path.
    SDL_GPUShader* GetVertexShader(const std::string_view path);

    /// @brief Retrieves a fragment shader by its path.
    SDL_GPUShader* GetFragShader(const std::string_view path);
    
    SDL_GPUSampler* m_Sampler = nullptr;

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
    std::deque<SDLMaterial*> m_Materials;
    std::unordered_map<MaterialId, size_t> m_MaterialIndexById;
};