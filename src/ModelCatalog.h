#pragma once

#include <string>
#include <vector>

#include "Error.h"
#include "Vertex.h"
#include "Mesh.h"
#include "Model.h"

/// @brief A catalog for loading and storing 3D model specifications.
class ModelCatalog
{
public:

    explicit ModelCatalog(RefPtr<GPUDevice> gpuDevice)
        : m_GpuDevice(gpuDevice)
    {
    }

    /// @brief Loads a model from file if not already loaded.
    Result<RefPtr<Model>> LoadModelFromFile(const CacheKey& cacheKey, const std::string& filePath);

    Result<RefPtr<Model>> CreateModel(const CacheKey& cacheKey, const ModelSpec& modelSpec);

    /// @brief Retrieves a previously loaded model.
    Result<RefPtr<Model>> GetModel(const CacheKey& cacheKey) const;

    /// @brief Checks if a model with the given key exists in the catalog.
    bool ContainsModel(const CacheKey& cacheKey) const { return m_ModelCache.Contains(cacheKey); }

    /// @brief Returns the number of models in the catalog.
    size_t Size() const { return m_ModelCache.Size(); }

private:

    Result<RefPtr<GpuTexture>> GetOrCreateTexture(const TextureSpec& textureSpec);
    Result<RefPtr<GpuVertexShader>> GetOrCreateVertexShader(const VertexShaderSpec& shaderSpec);
    Result<RefPtr<GpuFragmentShader>> GetOrCreateFragmentShader(const FragmentShaderSpec& shaderSpec);

    template<typename Value>
    class ResourceCache
    {
    public:
        ResourceCache() = default;
        ~ResourceCache() = default;
        bool TryAdd(const CacheKey& key, Value& value)
        {
            auto it = std::lower_bound(
                m_Keys.begin(),
                m_Keys.end(),
                key);

            if(it != m_Keys.end() && *it == key)
            {
                return false;
            }

            const size_t index = std::distance(m_Keys.begin(), it);
            m_Keys.insert(it, key);
            m_Values.insert(m_Values.begin() + index, value);
            return true;
        }

        bool TryGet(const CacheKey& key, Value& outValue) const
        {
            auto it = std::lower_bound(
                m_Keys.begin(),
                m_Keys.end(),
                key);

            if(it == m_Keys.end() || *it != key)
            {
                return false;
            }

            const size_t index = std::distance(m_Keys.begin(), it);
            outValue = m_Values[index];
            return true;
        }

        bool TryRemove(const CacheKey& key)
        {
            auto it = std::lower_bound(
                m_Keys.begin(),
                m_Keys.end(),
                key);

            if(it == m_Keys.end() || *it != key)
            {
                return false;
            }

            const size_t index = std::distance(m_Keys.begin(), it);
            m_Keys.erase(it);
            m_Values.erase(m_Values.begin() + index);
            return true;
        }

        bool Contains(const CacheKey& key) const
        {
            auto it = std::lower_bound(
                m_Keys.begin(),
                m_Keys.end(),
                key);

            return it != m_Keys.end() && *it == key;
        }

        size_t Size() const
        {
            return m_Keys.size();
        }

    private:
        std::vector<CacheKey> m_Keys;
        std::vector<Value> m_Values;
    };

    RefPtr<GPUDevice> m_GpuDevice;
    ResourceCache<RefPtr<Model>> m_ModelCache;
    ResourceCache<RefPtr<GpuTexture>> m_TextureCache;
    ResourceCache<RefPtr<GpuVertexShader>> m_VertexShaderCache;
    ResourceCache<RefPtr<GpuFragmentShader>> m_FragmentShaderCache;
};
