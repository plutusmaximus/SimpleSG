#pragma once

#include "Error.h"
#include "Mesh.h"
#include "Model.h"

#include <memory>

template<typename T>
class ResourceResult
{
    friend class ResourceCache;

public:

    bool IsPending() const
    {
        return m_ResultOrState.index() == 1 && !IsCompleted();
    }

    bool IsCompleted() const
    {
        if(m_ResultOrState.index() == 0)
        {
            return true;
        }

        return m_ResultOrState.get<1>()->m_IsCompleted;
    }

    const Result<T>& GetResult() const
    {
        if(!IsCompleted())
        {
            return Error("Attempted to get result of a pending resource.");
        }

        if(m_ResultOrState.index() == 0)
        {
            return m_ResultOrState.get<0>().GetResult();
        }

        return m_ResultOrState.get<1>()->m_Result;
    }

private:

    struct State
    {
        Result<T> m_Result;

        bool m_IsCompleted{ false };
    };

    ResourceResult() = delete;

    ResourceResult(const Result<T>& result)
        : m_ResultOrState(result)
    {
    }

    ResourceResult(Result<T>&& result)
        : m_ResultOrState(std::move(result))
    {
    }

    ResourceResult(const T& value)
        : m_ResultOrState(Result<T>(value))
    {
    }

    ResourceResult(T&& value)
        : m_ResultOrState(Result<T>(std::move(value)))
    {
    }

    ResourceResult(const Error& error)
        : m_ResultOrState(Result<T>(error))
    {
    }

    ResourceResult(Error&& error)
        : m_ResultOrState(Result<T>(std::move(error)))
    {
    }

    ResourceResult(const std::shared_ptr<State>& state)
        : m_ResultOrState(state)
    {
    }

    std::variant<Result<T>, std::shared_ptr<State>> m_ResultOrState;
};

/// @brief A cache for loading and storing GPU resources like models, textures, and shaders.
class ResourceCache
{
public:

    ResourceCache() = delete;
    ResourceCache(const ResourceCache&) = delete;
    ResourceCache& operator=(const ResourceCache&) = delete;
    ResourceCache(ResourceCache&&) = delete;
    ResourceCache& operator=(ResourceCache&&) = delete;

    explicit ResourceCache(GpuDevice* gpuDevice)
        : m_GpuDevice(gpuDevice)
    {
    }

    ~ResourceCache();

    /// @brief Loads a model from file if not already loaded.
    Result<Model> LoadModelFromFile(const CacheKey& cacheKey, std::string_view filePath);

    /// @brief Creates a model from the given specification if not already created.
    Result<Model> GetOrCreateModel(const CacheKey& cacheKey, const ModelSpec& modelSpec);

    /// @brief Retrieves or creates a texture (if not already cached) from the given specification.
    Result<Texture> GetOrCreateTexture(const TextureSpec& textureSpec);

    /// @brief Retrieves or creates a vertex shader (if not already cached) from the given specification.
    Result<VertexShader> GetOrCreateVertexShader(const VertexShaderSpec& shaderSpec);

    /// @brief Retrieves or creates a fragment shader (if not already cached) from the given specification.
    Result<FragmentShader> GetOrCreateFragmentShader(const FragmentShaderSpec& shaderSpec);

    /// @brief Retrieves a cached model.
    Result<Model> GetModel(const CacheKey& cacheKey) const;

    /// @brief Retrieves a cached texture.
    Result<Texture> GetTexture(const CacheKey& cacheKey) const;

    /// @brief Retrieves a cached vertex shader.
    Result<VertexShader> GetVertexShader(const CacheKey& cacheKey) const;

    /// @brief Retrieves a cached fragment shader.
    Result<FragmentShader> GetFragmentShader(const CacheKey& cacheKey) const;

private:

    /// @brief Loads a texture from the given file path.
    Result<Texture> CreateTexture(const std::string_view path);

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
        bool TryAdd(const CacheKey& key, const Value& value)
        {
            auto it = Find(key);

            if(it != m_Entries.end() && it->Key == key)
            {
                return false;
            }

            m_Entries.insert(it, Entry{key, value});
            return true;
        }

        bool TryGet(const CacheKey& key, Value& value) const
        {
            auto it = Find(key);

            if(it == m_Entries.end() || it->Key != key)
            {
                return false;
            }

            value = it->Value;
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

        Iterator begin()
        {
            return m_Entries.begin();
        }

        ConstIterator begin() const
        {
            return m_Entries.begin();
        }

        Iterator end()
        {
            return m_Entries.end();
        }

        ConstIterator end() const
        {
            return m_Entries.end();
        }

    private:
        std::vector<Entry> m_Entries;
    };

    GpuDevice* const m_GpuDevice;
    std::vector<VertexBuffer> m_VertexBuffers;
    std::vector<IndexBuffer> m_IndexBuffers;
    Cache<Model> m_ModelCache;
    Cache<Texture> m_TextureCache;
    Cache<VertexShader> m_VertexShaderCache;
    Cache<FragmentShader> m_FragmentShaderCache;
};
