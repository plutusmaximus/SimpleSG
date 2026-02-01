#pragma once

#include "Error.h"
#include "FileIo.h"
#include "Mesh.h"
#include "Model.h"

#include <memory>

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

    /// @brief Checks if the asynchronous operation is still pending.
    bool IsPending(const CacheKey& cacheKey) const;

    /// @brief Processes pending asynchronous operations.
    void ProcessPendingOperations();

    /// @brief Loads a model from file asynchronously if not already loaded.
    Result<void> LoadModelFromFileAsync(const CacheKey& cacheKey, std::string_view filePath);

    /// @brief Loads a model from file if not already loaded.
    Result<Model> LoadModelFromFile(const CacheKey& cacheKey, std::string_view filePath);

    /// @brief Loads a model from memory if not already loaded.
    Result<Model> LoadModelFromMemory(
        const CacheKey& cacheKey, const std::span<const uint8_t> data, std::string_view filePath);

    /// @brief Creates a model from the given specification if not already created.
    Result<Model> GetOrCreateModel(const CacheKey& cacheKey, const ModelSpec& modelSpec);

    Result<void> CreateTextureAsync(
        const CacheKey& cacheKey, const TextureSpec& textureSpec);

    /// @brief Retrieves or creates a texture (if not already cached) from the given specification.
    Result<Texture> GetOrCreateTexture(const TextureSpec& textureSpec);

    /// @brief Retrieves or creates a vertex shader (if not already cached) from the given
    /// specification.
    Result<VertexShader> GetOrCreateVertexShader(const VertexShaderSpec& shaderSpec);

    /// @brief Retrieves or creates a fragment shader (if not already cached) from the given
    /// specification.
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

    /// @brief Base class for asynchronous operations.
    class AsyncOp
    {
    public:
        virtual ~AsyncOp() = default;

        virtual void Start() = 0;

        virtual void Update() = 0;

        virtual bool IsComplete() const = 0;

        const CacheKey& GetCacheKey() const { return m_CacheKey; }

        void Enqueue(AsyncOp** head)
        {
            eassert(m_Next == nullptr);
            eassert(m_Prev == nullptr);

            Link(*head);
            *head = this;
        }

        void Dequeue(AsyncOp** head)
        {
            eassert(m_Next != nullptr || m_Prev != nullptr || *head == this);

            if(*head == this)
            {
                *head = m_Next;
            }
            Unlink();
        }

        void Link(AsyncOp* next)
        {
            eassert(!m_Next);
            eassert(!m_Prev);

            m_Next = next;
            if(next)
            {
                next->m_Prev = this;
            }
        }

        void Unlink()
        {
            if(m_Prev)
            {
                m_Prev->m_Next = m_Next;
            }
            if(m_Next)
            {
                m_Next->m_Prev = m_Prev;
            }
            m_Next = nullptr;
            m_Prev = nullptr;
        }

        AsyncOp* Next() { return m_Next; }

    protected:

        explicit AsyncOp(const CacheKey& cacheKey)
            : m_CacheKey(cacheKey)
        {
        }

    private:

        CacheKey m_CacheKey;

        AsyncOp* m_Next{ nullptr };
        AsyncOp* m_Prev{ nullptr };
    };

    /// @brief Asynchronous operation for loading a model from file.
    class LoadModelOp : public AsyncOp
    {
        static constexpr const char* CLASS_NAME = "LoadModelOp";

    public:
        LoadModelOp(
            ResourceCache* resourceCache, const CacheKey& cacheKey, const std::string_view path)
            : AsyncOp(cacheKey),
              m_ResourceCache(resourceCache),
              m_Path(path)
        {
        }

        void Start() override;

        void Update() override;

        bool IsComplete() const override { return m_State == Completed; }

        const Result<CacheKey>& GetResult() const
        {
            eassert(IsComplete());
            return m_Result;
        }

    private:
        void SetResult(const Result<CacheKey>& result)
        {
            m_Result = result;
            m_State = Completed;
        }

        Result<CacheKey> LoadModel(const Result<FileIo::FetchDataPtr>& fileData);

        ResourceCache* m_ResourceCache{ nullptr };

        enum State
        {
            NotStarted,
            LoadingFile,
            Completed,
        };

        State m_State{ NotStarted };

        imstring m_Path;

        FileIo::AsyncToken m_FileFetchToken;

        Result<CacheKey> m_Result;
    };

    /// @brief Asynchronous operation for creating a texture.
    class CreateTextureOp : public AsyncOp
    {
        static constexpr const char* CLASS_NAME = "CreateTextureOp";

    public:
        CreateTextureOp(
            ResourceCache* resourceCache, const CacheKey& cacheKey, const TextureSpec& textureSpec)
            : AsyncOp(cacheKey),
              m_ResourceCache(resourceCache),
              m_TextureSpec(textureSpec)
        {
        }

        void Start() override;

        void Update() override;

        bool IsComplete() const override { return m_State == Completed; }

        const Result<CacheKey>& GetResult() const
        {
            eassert(IsComplete());
            return m_Result;
        }

    private:
        void SetResult(const Result<CacheKey>& result)
        {
            m_Result = result;
            m_State = Completed;
        }

        Result<void> AddDummyTextureToCache();

        Result<Texture> CreateTexture(const FileIo::FetchDataPtr& fetchDataPtr);

        void AddOrReplaceInCache(const Texture& texture);

        ResourceCache* m_ResourceCache{ nullptr };

        enum State
        {
            NotStarted,
            LoadingFile,
            Completed,
        };

        State m_State{ NotStarted };

        TextureSpec m_TextureSpec;

        FileIo::AsyncToken m_FileFetchToken;

        Result<CacheKey> m_Result;
    };

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

            m_Entries.insert(it, Entry{ key, value });
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

        void AddOrReplace(const CacheKey& key, const Value& value)
        {
            auto it = Find(key);

            if(it != m_Entries.end() && it->Key == key)
            {
                it->Value = value;
            }
            else
            {
                m_Entries.insert(it, Entry{ key, value });
            }
        }

        bool Contains(const CacheKey& key) const
        {
            auto it = Find(key);

            return it != m_Entries.end() && it->Key == key;
        }

        size_t Size() const { return m_Entries.size(); }

        Iterator Find(const CacheKey& key)
        {
            return std::lower_bound(m_Entries.begin(),
                m_Entries.end(),
                key,
                [](const Entry& entry, const CacheKey& key) { return entry.Key < key; });
        }

        ConstIterator Find(const CacheKey& key) const
        {
            return std::lower_bound(m_Entries.begin(),
                m_Entries.end(),
                key,
                [](const Entry& entry, const CacheKey& key) { return entry.Key < key; });
        }

        Iterator begin() { return m_Entries.begin(); }

        ConstIterator begin() const { return m_Entries.begin(); }

        Iterator end() { return m_Entries.end(); }

        ConstIterator end() const { return m_Entries.end(); }

    private:
        std::vector<Entry> m_Entries;
    };

    /// @brief Loads a texture from the given file path.
    Result<Texture> CreateTexture(const std::string_view path);

    GpuDevice* const m_GpuDevice;
    std::vector<VertexBuffer> m_VertexBuffers;
    std::vector<IndexBuffer> m_IndexBuffers;
    Cache<Model> m_ModelCache;
    Cache<Texture> m_TextureCache;
    Cache<VertexShader> m_VertexShaderCache;
    Cache<FragmentShader> m_FragmentShaderCache;

    AsyncOp* m_PendingOps{ nullptr };
};
