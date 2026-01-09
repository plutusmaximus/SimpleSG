#pragma once

#include <string>
#include <vector>

#include "Error.h"
#include "Vertex.h"
#include "Mesh.h"
#include "Model.h"

/// @brief A cache for loading and storing GPU resources like models, textures, and shaders.
class ResourceCache
{
public:

    explicit ResourceCache(RefPtr<GPUDevice> gpuDevice)
        : m_GpuDevice(gpuDevice)
    {
    }

    /// @brief Loads a model from file if not already loaded.
    Result<RefPtr<Model>> LoadModelFromFile(const CacheKey& cacheKey, std::string_view filePath);

    /// @brief Creates a model from the given specification if not already created.
    Result<RefPtr<Model>> GetOrCreateModel(const CacheKey& cacheKey, const ModelSpec& modelSpec);

    /// @brief Retrieves or creates a texture (if not already cached) from the given specification.
    Result<RefPtr<GpuTexture>> GetOrCreateTexture(const TextureSpec& textureSpec);

    /// @brief Retrieves or creates a vertex shader (if not already cached) from the given specification.
    Result<RefPtr<GpuVertexShader>> GetOrCreateVertexShader(const VertexShaderSpec& shaderSpec);

    /// @brief Retrieves or creates a fragment shader (if not already cached) from the given specification.
    Result<RefPtr<GpuFragmentShader>> GetOrCreateFragmentShader(const FragmentShaderSpec& shaderSpec);

    /// @brief Retrieves a cached model.
    Result<RefPtr<Model>> GetModel(const CacheKey& cacheKey) const;

    /// @brief Retrieves a cached texture.
    Result<RefPtr<GpuTexture>> GetTexture(const CacheKey& cacheKey) const;

    /// @brief Retrieves a cached vertex shader.
    Result<RefPtr<GpuVertexShader>> GetVertexShader(const CacheKey& cacheKey) const;

    /// @brief Retrieves a cached fragment shader.
    Result<RefPtr<GpuFragmentShader>> GetFragmentShader(const CacheKey& cacheKey) const;

private:

    /// @brief Loads a texture from the given file path.
    Result<RefPtr<GpuTexture>> CreateTexture(const std::string_view path);

    template<typename Value>
    class Cache
    {
    public:
        struct Entry
        {
            CacheKey Key;
            Value Value;
        };

        using Iterator = typename std::vector<Entry>::iterator;
        using ConstIterator = typename std::vector<Entry>::const_iterator;

        Cache() = default;
        ~Cache() = default;
        bool TryAdd(const CacheKey& key, Value& value)
        {
            auto it = Find(key);

            if(it != m_Entries.end() && it->Key == key)
            {
                return false;
            }

            const size_t index = std::distance(m_Entries.begin(), it);
            m_Entries.insert(it, Entry{key, value});
            return true;
        }

        bool TryGet(const CacheKey& key, Value& outValue) const
        {
            auto it = Find(key);

            if(it == m_Entries.end() || it->Key != key)
            {
                return false;
            }

            const size_t index = std::distance(m_Entries.begin(), it);
            outValue = it->Value;
            return true;
        }

        bool TryRemove(const CacheKey& key)
        {
            auto it = Find(key);

            if(it == m_Entries.end() || it->Key != key)
            {
                return false;
            }

            const size_t index = std::distance(m_Entries.begin(), it);
            m_Entries.erase(it);
            return true;
        }

        bool Contains(const CacheKey& key) const
        {
            auto it = Find(key);

            return it != m_Entries.end() && it->Key == key;
        }

        size_t Size() const
        {
            return m_Entries.size();
        }

        Iterator Find(const CacheKey& key)
        {
            return std::lower_bound(
                m_Entries.begin(),
                m_Entries.end(),
                key,
                [](const Entry& entry, const CacheKey& key)
                {
                    return entry.Key < key;
                });
        }

        ConstIterator Find(const CacheKey& key) const
        {
            return std::lower_bound(
                m_Entries.begin(),
                m_Entries.end(),
                key,
                [](const Entry& entry, const CacheKey& key)
                {
                    return entry.Key < key;
                });
        }

    private:
        std::vector<Entry> m_Entries;
    };

    RefPtr<GPUDevice> m_GpuDevice;
    Cache<RefPtr<Model>> m_ModelCache;
    Cache<RefPtr<GpuTexture>> m_TextureCache;
    Cache<RefPtr<GpuVertexShader>> m_VertexShaderCache;
    Cache<RefPtr<GpuFragmentShader>> m_FragmentShaderCache;
};
