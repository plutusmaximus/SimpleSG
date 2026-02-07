#pragma once

#include "FileIo.h"
#include "Mesh.h"
#include "Model.h"
#include "PoolAllocator.h"
#include "Result.h"

#include <atomic>
#include <memory>
#include <optional>
#include <variant>

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

    /// @brief Creates a texture asynchronously if not already created.
    Result<void> CreateTextureAsync(
        const CacheKey& cacheKey, const TextureSpec& textureSpec);

    /// @brief Retrieves or creates a vertex shader (if not already cached) from the given
    /// specification.
    Result<GpuVertexShader*> GetOrCreateVertexShader(const VertexShaderSpec& shaderSpec);

    /// @brief Retrieves or creates a fragment shader (if not already cached) from the given
    /// specification.
    Result<GpuFragmentShader*> GetOrCreateFragmentShader(const FragmentShaderSpec& shaderSpec);

    /// @brief Retrieves a cached model.
    Result<Model> GetModel(const CacheKey& cacheKey) const;

    /// @brief Retrieves a cached texture.
    Result<GpuTexture*> GetTexture(const CacheKey& cacheKey) const;

    /// @brief Retrieves a cached vertex shader.
    Result<GpuVertexShader*> GetVertexShader(const CacheKey& cacheKey) const;

    /// @brief Retrieves a cached fragment shader.
    Result<GpuFragmentShader*> GetFragmentShader(const CacheKey& cacheKey) const;

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

        ~CreateTextureOp() override;

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

        Result<void> DecodeImage();

        Result<GpuTexture*> CreateTexture();

        void AddToCache(Result<GpuTexture*> texture);

        ResourceCache* m_ResourceCache{ nullptr };

        enum State
        {
            NotStarted,
            LoadingFile,
            DecodingImage,
            Completed,
        };

        State m_State{ NotStarted };

        TextureSpec m_TextureSpec;

        FileIo::AsyncToken m_FileFetchToken;

        FileIo::FetchDataPtr m_FetchDataPtr;

        Result<CacheKey> m_Result;

        void* m_DecodedImageData{ nullptr };
        int m_DecodedImageWidth{ 0 };
        int m_DecodedImageHeight{ 0 };
        int m_DecodedImageChannels{ 0 };

        std::optional<Result<void>> m_DecodeImageResult;
        std::atomic<bool> m_DecodeImageComplete{ false };
    };

    /// @brief Union for storing different types of asynchronous operations.
    /// Used with PoolAllocator to manage memory for various AsyncOp types.
    /// If new AsyncOp types are added, they must be included here.
    union AsyncOpUnion
    {
        uint8_t LoadModelOp[sizeof(LoadModelOp)];
        uint8_t CreateTextureOp[sizeof(CreateTextureOp)];
    };

    PoolAllocator<AsyncOpUnion, 16> m_AsyncOpAllocator;

    /// @brief Allocates an asynchronous operation from the pool.
    /// Passes the constructor arguments to the operation.
    template<typename T, typename... Args>
    T* AllocateOp(Args&&... args)
    {
        AsyncOpUnion* opUnion = m_AsyncOpAllocator.Alloc();
        return ::new (opUnion) T(std::forward<Args>(args)...);
    }

    /// @brief Frees an asynchronous operation back to the pool.
    template<typename T>
    void FreeOp(T* op)
    {
        op->~T();
        m_AsyncOpAllocator.Free(reinterpret_cast<AsyncOpUnion*>(op));
    }

    template<typename ValueT>
    class Cache
    {
    public:
        struct Entry
        {
            CacheKey Key;
            Result<ValueT> Result;
        };

        using Iterator = typename std::vector<Entry>::iterator;
        using ConstIterator = typename std::vector<Entry>::const_iterator;

        Cache() = default;
        ~Cache() = default;
        bool TryAdd(const CacheKey& key, const Result<ValueT>& result)
        {
            auto it = Find(key);

            if(it != m_Entries.end() && it->Key == key)
            {
                //Already exists
                return false;
            }

            m_Entries.insert(it, Entry{ key, result });

            return true;
        }

        bool TryGet(const CacheKey& key, Result<ValueT>& outResult) const
        {
            auto it = Find(key);

            if(it == m_Entries.end() || it->Key != key)
            {
                return false;
            }

            outResult = it->Result;
            return true;
        }

        bool Contains(const CacheKey& key) const
        {
            auto it = Find(key);

            return it != m_Entries.end() && it->Key == key;
        }

        void Remove(const CacheKey& key)
        {
            auto it = Find(key);

            if(it != m_Entries.end() && it->Key == key)
            {
                m_Entries.erase(it);
            }
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

    GpuDevice* const m_GpuDevice;
    std::vector<GpuVertexBuffer*> m_VertexBuffers;
    std::vector<GpuIndexBuffer*> m_IndexBuffers;
    Cache<Model> m_ModelCache;
    Cache<GpuTexture*> m_TextureCache;
    Cache<GpuVertexShader*> m_VertexShaderCache;
    Cache<GpuFragmentShader*> m_FragmentShaderCache;

    AsyncOp* m_PendingOps{ nullptr };
};
