#pragma once

#include "FileIo.h"
#include "Mesh.h"
#include "Model.h"
#include "PoolAllocator.h"
#include "Result.h"

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
            return m_IsPendingFunc(m_ResourceCache, m_CacheKey);
        }

        const CacheKey& GetCacheKey() const { return m_CacheKey; }

    private:
        AsyncStatus(ResourceCache* resourceCache,
            const CacheKey& cacheKey,
            bool (*isPendingFunc)(ResourceCache* cache, const CacheKey& cacheKey))
            : m_ResourceCache(resourceCache),
                m_CacheKey(cacheKey),
                m_IsPendingFunc(isPendingFunc)
        {
        }

        bool (*m_IsPendingFunc)(ResourceCache* cache, const CacheKey& cacheKey);

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

    class AsyncOp;

    /// @brief A group of asynchronous operations that are related and should be processed together.
    /// As long as any operation in the group is pending, the group is considered pending.
    class AsyncOpGroup
    {
    public:

        ~AsyncOpGroup()
        {
            eassert(!IsPending(), "AsyncOpGroup destroyed while operations still pending");
        }

        bool IsPending() const
        {
            return nullptr != m_Head;
        }

        AsyncOp* m_Head{ nullptr };

        AsyncOpGroup* m_Next { nullptr };
    };

    union AsyncOpUnion;

    /// @brief Base class for asynchronous operations.
    class AsyncOp
    {
        friend class ResourceCache;
    public:
        virtual ~AsyncOp()
        {
            eassert(!m_Next && !m_Prev, "AsyncOp destroyed while still pending");
            eassert(!m_NextInGroup && !m_PrevInGroup && !m_Group,
                "AsyncOp destroyed while still part of a group");
            eassert(!m_Group, "AsyncOp destroyed while still part of a group");
        }

        virtual void Start() = 0;

        virtual void Update() = 0;

        virtual bool IsStarted() const = 0;
        virtual bool IsPending() const = 0;
        virtual bool IsComplete() const = 0;

        const CacheKey& GetCacheKey() const { return m_CacheKey; }

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

        void AddToGroup(AsyncOpGroup* group)
        {
            eassert(!m_NextInGroup);
            eassert(!m_PrevInGroup);
            eassert(!m_Group);

            m_Group = group;

            m_NextInGroup = group->m_Head;
            if(group->m_Head)
            {
                group->m_Head->m_PrevInGroup = this;
            }
            group->m_Head = this;
        }

        void RemoveFromGroup()
        {
            if(!m_Group)
            {
                return;
            }

            eassert(m_PrevInGroup || m_NextInGroup || this == m_Group->m_Head, "Invalid state");

             AsyncOpGroup* group = m_Group;
             m_Group = nullptr;

            if(m_PrevInGroup)
            {
                m_PrevInGroup->m_NextInGroup = m_NextInGroup;
            }
            else
            {
                eassert(this == group->m_Head);
                group->m_Head = m_NextInGroup;
            }

            if(m_NextInGroup)
            {
                m_NextInGroup->m_PrevInGroup = m_PrevInGroup;
            }

            m_NextInGroup = nullptr;
            m_PrevInGroup = nullptr;
        }

    protected:
        explicit AsyncOp(const CacheKey& cacheKey)
            : m_CacheKey(cacheKey)
        {
        }

        template<typename U>
        void SetDeleter(void (*deleter)(AsyncOp*, U* userData), U* userData)
        {
            m_DeleterInvoker = [](AsyncOp* op, void (*cb)(AsyncOp*, void* userData), void* userData)
            {
                auto deleter = reinterpret_cast<void (*)(AsyncOp*, U*)>(cb);
                auto typedUserData = static_cast<U*>(userData);
                deleter(op, typedUserData);
            };
            m_Deleter = reinterpret_cast<void (*)(AsyncOp*, void*)>(deleter);
            m_DeleterUserData = userData;
        }

        void InvokeDeleter()
        {
            if(m_DeleterInvoker)
            {
                m_DeleterInvoker(this, m_Deleter, m_DeleterUserData);
            }
        }

    private:
        CacheKey m_CacheKey;

        AsyncOp* m_Next{ nullptr };
        AsyncOp* m_Prev{ nullptr };

        AsyncOp* m_NextInGroup{ nullptr };
        AsyncOp* m_PrevInGroup{ nullptr };

        /// Group that this operation is part of.
        AsyncOpGroup* m_Group{ nullptr };

        void (*m_DeleterInvoker)(AsyncOp* op, void (*cb)(AsyncOp*, void* userData), void* userData){ nullptr };
        void (*m_Deleter)(AsyncOp* op, void* userData){ nullptr };
        void* m_DeleterUserData{ nullptr };
    };

    /// @brief Enqueues an asynchronous operation for processing.
    void Enqueue(AsyncOp* op);

    void PushGroup(AsyncOpGroup* group)
    {
        eassert(!group->m_Head);
        eassert(!group->m_Next);

        group->m_Next = m_AsyncOpGroupStack;
        m_AsyncOpGroupStack = group;
    }

    void PopGroup(AsyncOpGroup* group)
    {
        eassert(m_AsyncOpGroupStack == group);
        m_AsyncOpGroupStack = group->m_Next;
        group->m_Next = nullptr;
    }

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

        void Update() override;

        bool IsStarted() const override { return m_State != NotStarted; }
        bool IsPending() const override { return m_State != Complete; }
        bool IsComplete() const override { return m_State == Complete; }

        const Result<ModelResource>& GetResult() const { return m_Result; }

        ResourceCache* GetResourceCache() const { return m_ResourceCache; }

    private:

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

        Result<ModelResource> m_Result;

        AsyncOpGroup m_OpGroup;

        Error m_FailError;
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

        void Start() override;

        void Update() override;

        bool IsStarted() const override { return m_State != NotStarted; }
        bool IsPending() const override { return m_State != Complete; }
        bool IsComplete() const override { return m_State == Complete; }

        const Result<ModelResource>& GetResult() const { return m_Result; }

        ResourceCache* GetResourceCache() const { return m_ResourceCache; }

    private:

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

        std::optional<CreateModelOp> m_CreateModelOp;

        FileIo::AsyncToken m_FileFetchToken;

        Result<ModelSpec> m_ModelSpecResult;

        Result<ModelResource> m_Result;
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

        const Result<GpuTexture*>& GetResult() const { return m_Result; }

        ResourceCache* GetResourceCache() const { return m_ResourceCache; }

    private:

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

        Result<GpuTexture*> m_Result;
    };

    /// @brief Asynchronous operation for creating a vertex shader.
    class CreateVertexShaderOp : public AsyncOp
    {
        static constexpr const char* CLASS_NAME = "CreateVertexShaderOp";

    public:
        CreateVertexShaderOp(ResourceCache* resourceCache,
            const CacheKey& cacheKey,
            const VertexShaderSpec& shaderSpec);

        ~CreateVertexShaderOp() override;

        void Start() override;

        void Update() override;

        bool IsStarted() const override { return m_State != NotStarted; }
        bool IsPending() const override { return m_State != Complete; }
        bool IsComplete() const override { return m_State == Complete; }

        const Result<GpuVertexShader*>& GetResult() const { return m_Result; }

        ResourceCache* GetResourceCache() const { return m_ResourceCache; }

    private:

        void SetResult(Result<GpuVertexShader*> result);

        Result<void> DecodeImage();

        Result<GpuVertexShader*> CreateVertexShader(const FileIo::FetchDataPtr& fetchData);

        ResourceCache* m_ResourceCache{ nullptr };

        enum State
        {
            NotStarted,
            LoadingFile,
            Complete,
        };

        State m_State{ NotStarted };

        VertexShaderSpec m_ShaderSpec;

        FileIo::AsyncToken m_FileFetchToken;

        Result<GpuVertexShader*> m_Result;
    };

    /// @brief Asynchronous operation for creating a vertex shader.
    class CreateFragmentShaderOp : public AsyncOp
    {
        static constexpr const char* CLASS_NAME = "CreateFragmentShaderOp";

    public:
        CreateFragmentShaderOp(ResourceCache* resourceCache,
            const CacheKey& cacheKey,
            const FragmentShaderSpec& shaderSpec);

        ~CreateFragmentShaderOp() override;

        void Start() override;

        void Update() override;

        bool IsStarted() const override { return m_State != NotStarted; }
        bool IsPending() const override { return m_State != Complete; }
        bool IsComplete() const override { return m_State == Complete; }

        const Result<GpuFragmentShader*>& GetResult() const { return m_Result; }

        ResourceCache* GetResourceCache() const { return m_ResourceCache; }

    private:

        void SetResult(Result<GpuFragmentShader*> result);

        Result<void> DecodeImage();

        Result<GpuFragmentShader*> CreateFragmentShader(const FileIo::FetchDataPtr& fetchData);

        ResourceCache* m_ResourceCache{ nullptr };

        enum State
        {
            NotStarted,
            LoadingFile,
            Complete,
        };

        State m_State{ NotStarted };

        FragmentShaderSpec m_ShaderSpec;

        FileIo::AsyncToken m_FileFetchToken;

        Result<GpuFragmentShader*> m_Result;
    };

    /// @brief Union for storing different types of asynchronous operations.
    /// Used with PoolAllocator to manage memory for various AsyncOp types.
    /// If new AsyncOp types are added, they must be included here.
    union AsyncOpUnion
    {
        uint8_t CreateModelOp[sizeof(CreateModelOp)];
        uint8_t LoadModelOp[sizeof(LoadModelOp)];
        uint8_t CreateTextureOp[sizeof(CreateTextureOp)];
        uint8_t CreateVertexShaderOp[sizeof(CreateVertexShaderOp)];
        uint8_t CreateFragmentShaderOp[sizeof(CreateFragmentShaderOp)];
    };

    PoolAllocator<AsyncOpUnion, 16> m_AsyncOpAllocator;

    /// @brief Allocates an asynchronous operation from the pool.
    /// Passes the constructor arguments to the operation.
    template<typename T, typename... Args>
    T* AllocateOp(Args&&... args)
    {
        static_assert(std::is_base_of_v<AsyncOp, T>, "T must be derived from AsyncOp");
        static_assert(sizeof(T) <= sizeof(AsyncOpUnion), "T must fit within AsyncOpUnion");
        AsyncOpUnion* opUnion = m_AsyncOpAllocator.Alloc();
        T* t = ::new (opUnion) T(std::forward<Args>(args)...);
        auto deleter = +[](AsyncOp* op, PoolAllocatorBase<AsyncOpUnion>* allocator)
        {
            T* t = static_cast<T*>(op);
            t->~T();
            allocator->Free(reinterpret_cast<AsyncOpUnion*>(t));
        };
        t->SetDeleter<PoolAllocatorBase<AsyncOpUnion>>(deleter, &m_AsyncOpAllocator);
        return t;
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

    AsyncOp* m_PendingOps{ nullptr };

    AsyncOpGroup* m_AsyncOpGroupStack{ nullptr };
};
