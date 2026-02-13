#pragma once

#include "FileIo.h"
#include "inlist.h"
#include "instack.h"
#include "Mesh.h"
#include "Model.h"
#include "PoolAllocator.h"
#include "Result.h"
#include "Stopwatch.h"

#include <atomic>
#include <optional>
#include <unordered_map>
#include <variant>

/// @brief Wrapper for a model resource in the cache.  By using a wrapper
/// we can control the constness of the model pointer and ensure consistency
/// in how the model is stored in the cache and how it's stored in the
/// entity database.
class ModelResource
{
public:

    explicit ModelResource(Model* model) : m_Model(model) {}

    const Model* operator->() const { return m_Model; }

    const Model* Get() const { return m_Model; }

    Model* Get() { return m_Model; }

private:
    Model* m_Model;
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

    class AsyncStatus
    {
        friend class ResourceCache;
    public:

        bool IsPending() const
        {
            return (m_ResourceCache->*m_IsPendingFunc)(m_CacheKey);
        }

        const CacheKey& GetCacheKey() const { return m_CacheKey; }

    private:
        AsyncStatus(ResourceCache* resourceCache,
            const CacheKey& cacheKey,
            bool (ResourceCache::*isPendingFunc)(const CacheKey&) const)
            : m_ResourceCache(resourceCache),
                m_CacheKey(cacheKey),
                m_IsPendingFunc(isPendingFunc)
        {
        }

        bool (ResourceCache::*m_IsPendingFunc)(const CacheKey&) const;

        ResourceCache* m_ResourceCache{ nullptr };
        CacheKey m_CacheKey;
    };

    ~ResourceCache();

    /// @brief Checks if the asynchronous operation is still pending.
    template<typename ResourceType>
    bool IsPending(const CacheKey& cacheKey) const;

    /// @brief Processes pending asynchronous operations.
    void ProcessPendingOperations();

    /// @brief Loads a model from file asynchronously if not already loaded.
    Result<AsyncStatus> LoadModelFromFileAsync(const CacheKey& cacheKey, const imstring& filePath);

    /// @brief Creates a model from the given specification if not already created.
    Result<AsyncStatus> CreateModelAsync(const CacheKey& cacheKey, const ModelSpec& modelSpec);

    /// @brief Creates a texture asynchronously if not already created.
    /// Call IsPending() to check if the operation is still pending.
    /// When complete use GetTexture() to retrieve the created texture.
    Result<AsyncStatus> CreateTextureAsync(
        const CacheKey& cacheKey, const TextureSpec& textureSpec);

    /// @brief Creates a vertex shader asynchronously if not already created.
    /// Call IsPending() to check if the operation is still pending.
    /// When complete use GetVertexShader() to retrieve the created vertex shader.
    Result<AsyncStatus> CreateVertexShaderAsync(
        const CacheKey& cacheKey, const VertexShaderSpec& shaderSpec);

    /// @brief Creates a fragment shader asynchronously if not already created.
    /// Call IsPending() to check if the operation is still pending.
    /// When complete use GetFragmentShader() to retrieve the created fragment shader.
    Result<AsyncStatus> CreateFragmentShaderAsync(
        const CacheKey& cacheKey, const FragmentShaderSpec& shaderSpec);

    /// @brief Retrieves a cached model.
    Result<ModelResource> GetModel(const CacheKey& cacheKey) const;

    /// @brief Retrieves a cached texture.
    Result<GpuTexture*> GetTexture(const CacheKey& cacheKey) const;

    /// @brief Retrieves a cached vertex shader.
    Result<GpuVertexShader*> GetVertexShader(const CacheKey& cacheKey) const;

    /// @brief Retrieves a cached fragment shader.
    Result<GpuFragmentShader*> GetFragmentShader(const CacheKey& cacheKey) const;

private:

    class AsyncOpGroup;

    /// @brief Base class for asynchronous operations.
    class AsyncOp
    {
        friend class ResourceCache;
    public:
        virtual ~AsyncOp()
        {
            eassert(!m_PendingOpsNode.IsLinked(), "AsyncOp destroyed while still pending");
            eassert(!m_GroupNode.IsLinked(), "AsyncOp destroyed while still part of a group");
            eassert(!m_Group, "AsyncOp destroyed while still part of a group");
        }

        virtual void Start() = 0;

        virtual void Update() = 0;

        virtual bool IsStarted() const = 0;
        virtual bool IsPending() const = 0;
        virtual bool IsComplete() const = 0;

        const CacheKey& GetCacheKey() const { return m_CacheKey; }

        void RemoveFromGroup()
        {
            if(!m_Group)
            {
                return;
            }

             m_Group->Remove(this);
        }

    protected:
        explicit AsyncOp(const CacheKey& cacheKey)
            : m_CacheKey(cacheKey)
        {
        }

        virtual void Delete() = 0;

    private:
        CacheKey m_CacheKey;

        inlist_node<AsyncOp> m_PendingOpsNode;

        inlist_node<AsyncOp> m_GroupNode;

        /// Group that this operation is part of.
        AsyncOpGroup* m_Group{ nullptr };
    };

    /// @brief A group of asynchronous operations that are related and should be processed together.
    /// As long as any operation in the group is pending, the group is considered pending.
    class AsyncOpGroup
    {
        friend class ResourceCache;
    public:

        ~AsyncOpGroup()
        {
            eassert(!IsPending(), "AsyncOpGroup destroyed while operations still pending");
        }

        bool IsPending() const
        {
            return !m_Operations.empty();
        }

        void Add(AsyncOp* op)
        {
            eassert(op->m_Group == nullptr, "Invalid state: op already part of a group");
            m_Operations.push_back(op);
            op->m_Group = this;
        }

        void Remove(AsyncOp* op)
        {
            eassert(op->m_Group == this, "Invalid state: op not part of this group");
            eassert(!m_Operations.empty(), "Invalid state: group is empty");

            m_Operations.erase(op);
            op->m_Group = nullptr;
        }

    private:

        inlist<AsyncOp, &AsyncOp::m_GroupNode> m_Operations;

        instack_node<AsyncOpGroup> m_GroupNode;
    };

    /// @brief Enqueues an asynchronous operation for processing.
    void Enqueue(AsyncOp* op);

    void PushGroup(AsyncOpGroup* group)
    {
        eassert(!group->IsPending(), "Cannot push group with pending operations");

        m_AsyncOpGroups.push(group);
    }

    void PopGroup(AsyncOpGroup* group)
    {
        eassert(m_AsyncOpGroups.top() == group, "Invalid state: group not at top of stack");

        m_AsyncOpGroups.pop();
    }

    /// @brief Asynchronous operation for waiting on another asynchronous operation to complete.
    /// This is used when, e.g., resource A depends on resource B, and when A attempts to load B, it
    /// finds that B is already being loaded by another operation.  In this case, A can enqueue a
    /// WaitOp that waits for B to finish loading, and then once the WaitOp completes, A can
    /// retrieve B from the cache and continue loading itself.
    class WaitOp : public AsyncOp
    {
        static constexpr const char* CLASS_NAME = "WaitOp";

    public:
        WaitOp(ResourceCache* resourceCache,
            const CacheKey& cacheKey,
            bool (ResourceCache::*isPendingFunc)(const CacheKey&) const)
            : AsyncOp(cacheKey),
              m_ResourceCache(resourceCache),
              m_IsPendingFunc(isPendingFunc)
        {
        }

        void Start() override;

        void Update() override;

        bool IsStarted() const override { return m_State != NotStarted; }
        bool IsPending() const override { return m_State != Complete; }
        bool IsComplete() const override { return m_State == Complete; }

    private:

        void Delete() override
        {
            m_ResourceCache->DeleteOp(this);
        }

        ResourceCache* m_ResourceCache{ nullptr };

        bool (ResourceCache::*m_IsPendingFunc)(const CacheKey&) const;

        enum State
        {
            NotStarted,
            Waiting,
            Complete,
        };

        State m_State{ NotStarted };
    };

    /// @brief Asynchronous operation for creating a model from a model spec.
    class CreateModelOp : public AsyncOp
    {
        static constexpr const char* CLASS_NAME = "CreateModelOp";

    public:
        CreateModelOp(
            ResourceCache* resourceCache, const CacheKey& cacheKey, const ModelSpec& modelSpec)
            : AsyncOp(cacheKey),
              m_ResourceCache(resourceCache),
              m_ModelSpec(modelSpec)
        {
        }

        ~CreateModelOp() override;

        void Start() override;

        // Call this when a parent async op handles caching the model.
        // This is used when loading a model from file, where the LoadModelOp will use
        // a CreateModelOp to create the model, but the LoadModelOp is responsible for caching it.
        void StartDoNotCache();

        void Update() override;

        bool IsStarted() const override { return m_State != NotStarted; }
        bool IsPending() const override { return m_State != Complete; }
        bool IsComplete() const override { return m_State == Complete; }

        const Result<ModelResource>& GetResult() const
        {
            eassert(IsComplete(), "GetResult() called before operation is complete");
            return m_Result;
        }

    private:

        void Delete() override
        {
            m_ResourceCache->DeleteOp(this);
        }

        Result<Model> CreateModel();

        void SetResult(Result<ModelResource> result);

        ResourceCache* m_ResourceCache{ nullptr };

        enum State
        {
            NotStarted,
            CreateVertexBuffer,
            CreateIndexBuffer,
            CreateTexturesAndShaders,
            CreatingTexturesAndShaders,
            Failed,
            Complete,
        };

        State m_State{ NotStarted };

        ModelSpec m_ModelSpec;

        GpuVertexBuffer* m_VertexBuffer{ nullptr };
        GpuIndexBuffer* m_IndexBuffer{ nullptr };

        AsyncOpGroup m_OpGroup;

        Error m_FailError;

        Result<ModelResource> m_Result;

        // Whether this operation should skip caching the created model in the cache.  This is used
        // when loading a model from file, where the LoadModelOp will reserve the cache entry and
        // set it when complete.
        bool m_DoNotCache{ false };
    };

    /// @brief Asynchronous operation for loading a model from file.
    class LoadModelOp : public AsyncOp
    {
        static constexpr const char* CLASS_NAME = "LoadModelOp";

    public:
        LoadModelOp(ResourceCache* resourceCache, const CacheKey& cacheKey, const imstring& path)
            : AsyncOp(cacheKey),
              m_ResourceCache(resourceCache),
              m_Path(path)
        {
        }

        ~LoadModelOp() override;

        void Start() override;

        void Update() override;

        bool IsStarted() const override { return m_State != NotStarted; }
        bool IsPending() const override { return m_State != Complete; }
        bool IsComplete() const override { return m_State == Complete; }

    private:

        void Delete() override
        {
            m_ResourceCache->DeleteOp(this);
        }

        void SetResult(Result<ModelResource> result);

        ResourceCache* m_ResourceCache{ nullptr };

        enum State
        {
            NotStarted,
            LoadFile,
            LoadingFile,
            CreateModel,
            CreatingModel,
            Complete,
        };

        State m_State{ NotStarted };

        imstring m_Path;

        CreateModelOp* m_CreateModelOp{nullptr};

        FileIo::AsyncToken m_FileFetchToken;

        Result<ModelSpec> m_ModelSpecResult;

        Stopwatch m_Stopwatch;
    };

    /// @brief Asynchronous operation for creating a texture.
    class CreateTextureOp : public AsyncOp
    {
        static constexpr const char* CLASS_NAME = "CreateTextureOp";

    public:
        CreateTextureOp(
            ResourceCache* resourceCache, const CacheKey& cacheKey, const TextureSpec& textureSpec);

        ~CreateTextureOp() override;

        void Start() override;

        void Update() override;

        bool IsStarted() const override { return m_State != NotStarted; }
        bool IsPending() const override { return m_State != Complete; }
        bool IsComplete() const override { return m_State == Complete; }

    private:

        void Delete() override
        {
            m_ResourceCache->DeleteOp(this);
        }

        void SetResult(Result<GpuTexture*> result);

        Result<void> DecodeImage();

        Result<GpuTexture*> CreateTexture();

        ResourceCache* m_ResourceCache{ nullptr };

        enum State
        {
            NotStarted,
            LoadingFile,
            DecodingImage,
            Complete,
        };

        State m_State{ NotStarted };

        TextureSpec m_TextureSpec;

        FileIo::AsyncToken m_FileFetchToken;

        FileIo::FetchDataPtr m_FetchDataPtr;

        void* m_DecodedImageData{ nullptr };
        int m_DecodedImageWidth{ 0 };
        int m_DecodedImageHeight{ 0 };
        int m_DecodedImageChannels{ 0 };

        std::optional<Result<void>> m_DecodeImageResult;
        std::atomic<bool> m_DecodeImageComplete{ false };
    };

    /// @brief Asynchronous operation for creating a vertex/fragment shader.
    class CreateShaderOp : public AsyncOp
    {
        static constexpr const char* CLASS_NAME = "CreateShaderOp";

    public:
        CreateShaderOp(ResourceCache* resourceCache,
            const CacheKey& cacheKey,
            const VertexShaderSpec& shaderSpec);

        CreateShaderOp(ResourceCache* resourceCache,
            const CacheKey& cacheKey,
            const FragmentShaderSpec& shaderSpec);

        ~CreateShaderOp() override;

        void Start() override;

        void Update() override;

        bool IsStarted() const override { return m_State != NotStarted; }
        bool IsPending() const override { return m_State != Complete; }
        bool IsComplete() const override { return m_State == Complete; }

    private:

        void Delete() override
        {
            m_ResourceCache->DeleteOp(this);
        }

        void SetResult(Result<GpuVertexShader*> result);

        void SetResult(Result<GpuFragmentShader*> result);

        void SetResult(const Error& result);

        Result<GpuVertexShader*> CreateVertexShader(const FileIo::FetchDataPtr& fetchData);

        Result<GpuFragmentShader*> CreateFragmentShader(const FileIo::FetchDataPtr& fetchData);

        ResourceCache* m_ResourceCache{ nullptr };

        enum State
        {
            NotStarted,
            LoadingVsFile,
            LoadingFsFile,
            Complete,
        };

        State m_State{ NotStarted };

        std::variant<VertexShaderSpec, FragmentShaderSpec> m_ShaderSpec;

        FileIo::AsyncToken m_FileFetchToken;
    };

    template<typename T>
    static constexpr size_t POOL_CAPACITY = 0;

    template<>
    constexpr size_t POOL_CAPACITY<WaitOp> = 1024;
    template<>
    constexpr size_t POOL_CAPACITY<CreateModelOp> = 256;
    template<>
    constexpr size_t POOL_CAPACITY<LoadModelOp> = 256;
    template<>
    constexpr size_t POOL_CAPACITY<CreateTextureOp> = 256;
    template<>
    constexpr size_t POOL_CAPACITY<CreateShaderOp> = 8;

    std::tuple<PoolAllocator<WaitOp, POOL_CAPACITY<WaitOp>>,
        PoolAllocator<CreateModelOp, POOL_CAPACITY<CreateModelOp>>,
        PoolAllocator<LoadModelOp, POOL_CAPACITY<LoadModelOp>>,
        PoolAllocator<CreateTextureOp, POOL_CAPACITY<CreateTextureOp>>,
        PoolAllocator<CreateShaderOp, POOL_CAPACITY<CreateShaderOp>>> m_OpPools;

    template<typename T>
    PoolAllocator<T, POOL_CAPACITY<T>>& GetAllocator()
    {
        static_assert(POOL_CAPACITY<T> > 0, "POOL_CAPACITY must be defined for this AsyncOp type");

        return std::get<PoolAllocator<T, POOL_CAPACITY<T>>>(m_OpPools);
    }

    /// @brief Allocates an asynchronous operation from the pool.
    /// Passes the constructor arguments to the operation.
    template<typename T, typename... Args>
    T* NewOp(Args&&... args)
    {
        static_assert(std::is_base_of_v<AsyncOp, T>, "T must be derived from AsyncOp");

        auto& allocator = GetAllocator<T>();

        T* t = allocator.New(std::forward<Args>(args)...);
        return t;
    }

    template<typename T>
    void DeleteOp(T* op)
    {
        static_assert(!std::is_same_v<AsyncOp, T>, "Can't delete a raw AsyncOp");
        static_assert(std::is_base_of_v<AsyncOp, T>, "T must be derived from AsyncOp");

        GetAllocator<T>().Delete(op);
    }

    template<typename ValueT>
    class Cache
    {
        struct Pending{};
        class CacheEntry : public std::variant<Pending, ValueT>
        {
        public:
            using std::variant<Pending, ValueT>::variant;

            bool IsPending() const { return std::holds_alternative<Pending>(*this); }

            ValueT& GetValue() { eassert(!IsPending()); return std::get<ValueT>(*this); }
            const ValueT& GetValue() const { eassert(!IsPending()); return std::get<ValueT>(*this); }
        };

        using MapType = std::unordered_map<CacheKey, CacheEntry>;

    public:

        using Iterator = typename MapType::iterator;
        using ConstIterator = typename MapType::const_iterator;

        Cache() = default;
        ~Cache() = default;

        bool IsPending(const CacheKey& key) const
        {
            auto it = m_Map.find(key);
            return it != m_Map.end() && it->second.IsPending();
        }

        bool TryReserve(const CacheKey& key)
        {
            return m_Map.try_emplace(key, CacheEntry{}).second;
        }

        bool Set(const CacheKey& key, const ValueT& value)
        {
            auto it = m_Map.find(key);

            if(!everify(it != m_Map.end(),
                   "Cache entry for key {} has not been reserved",
                   key.ToString()))
            {
                return false;
            }

            if(!everify(it->second.IsPending(),
                   "Cache entry for key {} is not pending",
                   key.ToString()))
            {
                return false;
            }

            it->second.emplace<ValueT>(value);
            return true;
        }

        bool TryGet(const CacheKey& key, ValueT& outValue) const
        {
            auto it = m_Map.find(key);

            if(it == m_Map.end())
            {
                return false;
            }

            if(it->second.IsPending())
            {
                return false;
            }

            outValue = std::get<ValueT>(it->second);
            return true;
        }

        bool Contains(const CacheKey& key) const
        {
            return m_Map.contains(key);
        }

        void Remove(const CacheKey& key)
        {
            m_Map.erase(key);
        }

        size_t Size() const { return m_Map.size(); }

        Iterator begin() { return m_Map.begin(); }

        ConstIterator begin() const { return m_Map.begin(); }

        Iterator end() { return m_Map.end(); }

        ConstIterator end() const { return m_Map.end(); }

    private:

        MapType m_Map;
    };

    PoolAllocator<Model, 1024> m_ModelAllocator;

    GpuDevice* const m_GpuDevice;

    Cache<Result<ModelResource>> m_ModelCache;
    Cache<Result<GpuTexture*>> m_TextureCache;
    Cache<Result<GpuVertexShader*>> m_VertexShaderCache;
    Cache<Result<GpuFragmentShader*>> m_FragmentShaderCache;

    inlist<AsyncOp, &AsyncOp::m_PendingOpsNode> m_PendingOps;

    instack<AsyncOpGroup, &AsyncOpGroup::m_GroupNode> m_AsyncOpGroups;
};
