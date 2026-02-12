#define __LOGGER_NAME__ "RSRC"

#include "ResourceCache.h"

#include "Logging.h"
#include "scope_exit.h"
#include "Stopwatch.h"
#include "ThreadPool.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

static constexpr const char* SHADER_EXTENSION = ".spv";

static constexpr const char* WHITE_TEXTURE_KEY = "#FFFFFFFF";
static constexpr const char* MAGENTA_TEXTURE_KEY = "#FF00FFFF";

static constexpr const RgbaColorf WHITE_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
static constexpr const RgbaColorf MAGENTA_COLOR(1.0f, 0.0f, 1.0f, 1.0f);

static const TextureSpec WHITE_TEXTURE_SPEC(WHITE_COLOR);
static const TextureSpec MAGENTA_TEXTURE_SPEC(MAGENTA_COLOR);

struct TextureProperty
{
    std::string Path;
    unsigned UVIndex = 0;
};

/// @brief Collection of texture properties for a material.
struct TextureProperties
{
    TextureProperty Albedo;
    TextureProperty Diffuse;
    TextureProperty Specular;
    TextureProperty Normal;
    TextureProperty Emission;
    TextureProperty Metalness;
    TextureProperty Roughness;
    TextureProperty AmbientOcclusion;
};

using SceneMeshId = unsigned;

/// @brief Collection of meshes in a scene.
using SceneMeshCollection = std::unordered_map<SceneMeshId, const aiMesh*>;

struct MeshSpecCollection
{
    imvector<MeshSpec>::builder MeshSpecs;
    std::unordered_map<SceneMeshId, int> MeshIdToSpecIndex;
};

/// @brief Retrieves texture properties from a given material.
static TextureProperties GetTexturePropertiesFromMaterial(const aiMaterial* material,
    const std::filesystem::path& parentPath);

/// @brief Retrieves the name of a mesh.
static inline std::string GetMeshName(const aiMesh* mesh);

/// @brief Logs information about a mesh.
static void LogMesh(const aiScene* scene, const unsigned meshIdx);

/// @brief Validates a mesh in a scene.
static bool ValidateMesh(const aiScene* scene, const unsigned meshIdx);

/// @brief Creates a mesh specification from a given mesh.
static MeshSpec CreateMeshSpecFromMesh(
    const aiScene* scene, const SceneMeshId meshId, const std::filesystem::path& parentPath);

/// @brief Recursively collects mesh specs from scene nodes.
static void CollectMeshSpecs(const aiScene* scene,
    const aiNode* node,
    MeshSpecCollection& outCollection,
    const std::filesystem::path& parentPath);

/// @brief Processes a scene node and its children.
static void ProcessNodes(const aiNode* node,
    const int parentNodeIndex,
    const MeshSpecCollection& meshSpecCollection,
    imvector<MeshInstance>::builder& meshInstances,
    imvector<TransformNode>::builder& transformNodes,
    const std::filesystem::path& parentPath);

/// @brief Creates a model specification from a scene.
static Result<ModelSpec>
ProcessScene(const aiScene* scene, const imstring& filePath);

ResourceCache::~ResourceCache()
{
    for(auto& it : m_ModelCache)
    {
        eassert(!it.second.IsPending(),
            "Model cache entry for key {} is still pending during ResourceCache destruction",
            it.first.ToString());

        // Release model resources
        auto model = it.second.GetValue().value().Get();
        m_ModelAllocator.Free(model);
    }

    for(auto& it : m_TextureCache)
    {
        eassert(!it.second.IsPending(),
            "Texture cache entry for key {} is still pending during ResourceCache destruction",
            it.first.ToString());

        auto texture = it.second.GetValue().value();
        auto result = m_GpuDevice->DestroyTexture(texture);
        if(!result)
        {
            logError("Failed to destroy texture: {}", result.error());
        }
    }

    for(auto& it : m_VertexShaderCache)
    {
        eassert(!it.second.IsPending(),
            "Vertex shader cache entry for key {} is still pending during ResourceCache destruction",
            it.first.ToString());

        // Release vertex shader resources
        auto vertexShader = it.second.GetValue().value();
        auto result = m_GpuDevice->DestroyVertexShader(vertexShader);
        if(!result)
        {
            logError("Failed to destroy vertex shader: {}", result.error());
        }
    }

    for(auto& it : m_FragmentShaderCache)
    {
        eassert(!it.second.IsPending(),
            "Fragment shader cache entry for key {} is still pending during ResourceCache destruction",
            it.first.ToString());

        // Release fragment shader resources
        auto fragmentShader = it.second.GetValue().value();
        auto result = m_GpuDevice->DestroyFragmentShader(fragmentShader);
        if(!result)
        {
            logError("Failed to destroy fragment shader: {}", result.error());
        }
    }
}

template<>
bool
ResourceCache::IsPending<ModelResource>(const CacheKey& cacheKey) const
{
    return m_ModelCache.IsPending(cacheKey);
}

template<>
bool
ResourceCache::IsPending<GpuTexture*>(const CacheKey& cacheKey) const
{
    return m_TextureCache.IsPending(cacheKey);
}

template<>
bool
ResourceCache::IsPending<GpuVertexShader*>(const CacheKey& cacheKey) const
{
    return m_VertexShaderCache.IsPending(cacheKey);
}

template<>
bool
ResourceCache::IsPending<GpuFragmentShader*>(const CacheKey& cacheKey) const
{
    return m_FragmentShaderCache.IsPending(cacheKey);
}

void
ResourceCache::ProcessPendingOperations()
{
    AsyncOp* op = m_PendingOps;

    while(op)
    {
        AsyncOp* next = op->m_Next;

        op->Update();

        if(op->IsComplete())
        {
            op->Unlink();
            if(op == m_PendingOps)
            {
                m_PendingOps = next;
            }
            op->RemoveFromGroup();
            op->InvokeDeleter();
        }

        op = next;
    }
}

static bool NotPendingFunc(ResourceCache* /*cache*/, const CacheKey& /*key*/)
{
    return false;
}

template<typename ResourceType>
static bool IsOpPending(ResourceCache* cache, const CacheKey& key)
{
    return cache->IsPending<ResourceType>(key);
}

Result<ResourceCache::AsyncStatus>
ResourceCache::LoadModelFromFileAsync(const CacheKey& cacheKey, const imstring& filePath)
{
    // Return existing entry without re-importing
    if(m_ModelCache.Contains(cacheKey))
    {
        logDebug("  Cache hit: {}", cacheKey.ToString());
        return AsyncStatus(this, cacheKey, NotPendingFunc);
    }

    expectv(!IsPending<ModelResource>(cacheKey),
        "  Model load already pending: {}",
        cacheKey.ToString());

    logDebug("  Cache miss: {}", cacheKey.ToString());

    auto op = AllocateOp<LoadModelOp>(this, cacheKey, filePath);
    expectv(op, "Failed to allocate LoadModelOp for key: {}", cacheKey.ToString());
    if(!everify(m_ModelCache.TryReserve(cacheKey)))
    {
        op->InvokeDeleter();
        return Error("Failed to reserve cache entry for key: {}", cacheKey.ToString());
    }
    Enqueue(op);

    return AsyncStatus(this, cacheKey, IsOpPending<ModelResource>);
}

Result<ResourceCache::AsyncStatus>
ResourceCache::CreateModelAsync(const CacheKey& cacheKey, const ModelSpec& modelSpec)
{
    // Return existing entry without re-importing
    if(m_ModelCache.Contains(cacheKey))
    {
        logDebug("  Cache hit: {}", cacheKey.ToString());
        return AsyncStatus(this, cacheKey, NotPendingFunc);
    }

    expectv(!IsPending<ModelResource>(cacheKey),
        "  Model creation already pending: {}",
        cacheKey.ToString());

    logDebug("  Cache miss: {}", cacheKey.ToString());

    auto op = AllocateOp<CreateModelOp>(this, cacheKey, modelSpec);
    expectv(op, "Failed to allocate CreateModelOp for key: {}", cacheKey.ToString());
    if(!everify(m_ModelCache.TryReserve(cacheKey)))
    {
        op->InvokeDeleter();
        return Error("Failed to reserve cache entry for key: {}", cacheKey.ToString());
    }
    Enqueue(op);

    return AsyncStatus(this, cacheKey, IsOpPending<ModelResource>);
}

/// @brief Creates a texture asynchronously if not already created.
Result<ResourceCache::AsyncStatus>
ResourceCache::CreateTextureAsync(const CacheKey& cacheKey, const TextureSpec& textureSpec)
{
    // Return existing entry without re-creating
    if(m_TextureCache.Contains(cacheKey))
    {
        logDebug("  Cache hit: {}", cacheKey.ToString());
        return AsyncStatus(this, cacheKey, NotPendingFunc);
    }

    expectv(!IsPending<GpuTexture*>(cacheKey),
        "  Texture creation already pending: {}",
        cacheKey.ToString());

    logDebug("  Cache miss: {}", cacheKey.ToString());

    auto op = AllocateOp<CreateTextureOp>(this, cacheKey, textureSpec);
    expectv(op, "Failed to allocate CreateTextureOp for key: {}", cacheKey.ToString());
    if(!everify(m_TextureCache.TryReserve(cacheKey)))
    {
        op->InvokeDeleter();
        return Error("Failed to reserve cache entry for key: {}", cacheKey.ToString());
    }
    Enqueue(op);

    return AsyncStatus(this, cacheKey, IsOpPending<GpuTexture*>);
}

Result<ResourceCache::AsyncStatus>
ResourceCache::CreateVertexShaderAsync(const CacheKey& cacheKey, const VertexShaderSpec& shaderSpec)
{
    // Return existing entry without re-creating
    if(m_VertexShaderCache.Contains(cacheKey))
    {
        logDebug("  Cache hit: {}", cacheKey.ToString());
        return AsyncStatus(this, cacheKey, NotPendingFunc);
    }

    expectv(!IsPending<GpuVertexShader*>(cacheKey),
        "  Vertex shader creation already pending: {}",
        cacheKey.ToString());

    logDebug("  Cache miss: {}", cacheKey.ToString());

    auto op = AllocateOp<CreateVertexShaderOp>(this, cacheKey, shaderSpec);
    expectv(op, "Failed to allocate CreateVertexShaderOp for key: {}", cacheKey.ToString());
    if(!everify(m_VertexShaderCache.TryReserve(cacheKey)))
    {
        op->InvokeDeleter();
        return Error("Failed to reserve cache entry for key: {}", cacheKey.ToString());
    }
    Enqueue(op);

    return AsyncStatus(this, cacheKey, IsOpPending<GpuVertexShader*>);
}

Result<ResourceCache::AsyncStatus>
ResourceCache::CreateFragmentShaderAsync(const CacheKey& cacheKey, const FragmentShaderSpec& shaderSpec)
{
    // Return existing entry without re-creating
    if(m_FragmentShaderCache.Contains(cacheKey))
    {
        logDebug("  Cache hit: {}", cacheKey.ToString());
        return AsyncStatus(this, cacheKey, NotPendingFunc);
    }

    expectv(!IsPending<GpuFragmentShader*>(cacheKey),
        "  Fragment shader creation already pending: {}",
        cacheKey.ToString());

    logDebug("  Cache miss: {}", cacheKey.ToString());

    auto op = AllocateOp<CreateFragmentShaderOp>(this, cacheKey, shaderSpec);
    expectv(op, "Failed to allocate CreateFragmentShaderOp for key: {}", cacheKey.ToString());
    if(!everify(m_FragmentShaderCache.TryReserve(cacheKey)))
    {
        op->InvokeDeleter();
        return Error("Failed to reserve cache entry for key: {}", cacheKey.ToString());
    }
    Enqueue(op);

    return AsyncStatus(this, cacheKey, IsOpPending<GpuFragmentShader*>);
}

Result<ModelResource>
ResourceCache::GetModel(const CacheKey& cacheKey) const
{
    Result<ModelResource> result;

    expect(m_ModelCache.TryGet(cacheKey, result), "Model not in cache: {}", cacheKey.ToString());

    return result;
}

Result<GpuTexture*>
ResourceCache::GetTexture(const CacheKey& cacheKey) const
{
    Result<GpuTexture*> result;

    expect(m_TextureCache.TryGet(cacheKey, result), "Texture not in cache: {}", cacheKey.ToString());

    return result;
}

Result<GpuVertexShader*>
ResourceCache::GetVertexShader(const CacheKey& cacheKey) const
{
    Result<GpuVertexShader*> result;

    expect(m_VertexShaderCache.TryGet(cacheKey, result), "Vertex shader not in cache: {}", cacheKey.ToString());

    return result;
}

Result<GpuFragmentShader*>
ResourceCache::GetFragmentShader(const CacheKey& cacheKey) const
{
    Result<GpuFragmentShader*> result;

    expect(m_FragmentShaderCache.TryGet(cacheKey, result), "Fragment shader not in cache: {}", cacheKey.ToString());

    return result;
}

// private:

void
ResourceCache::Enqueue(AsyncOp* op)
{
    if(m_AsyncOpGroupStack)
    {
        op->AddToGroup(m_AsyncOpGroupStack);
    }

    op->Link(m_PendingOps);
    m_PendingOps = op;

    op->Start();
}

// === ResourceCache::CreateModelOp ===

#define logOp(fmt, ...) logDebug("  {}: {}", CLASS_NAME, std::format(fmt, __VA_ARGS__))

ResourceCache::CreateModelOp::~CreateModelOp()
{
    eassert(!m_VertexBuffer);
    eassert(!m_IndexBuffer);

    if(m_VertexBuffer)
    {
        auto result = m_ResourceCache->m_GpuDevice->DestroyVertexBuffer(m_VertexBuffer);
        if(!result)
        {
            logError("Failed to destroy vertex buffer: {}", result.error());
        }
    }

    if(m_IndexBuffer)
    {
        auto result = m_ResourceCache->m_GpuDevice->DestroyIndexBuffer(m_IndexBuffer);
        if(!result)
        {
            logError("Failed to destroy index buffer: {}", result.error());
        }
    }
}

void
ResourceCache::CreateModelOp::Start()
{
    eassert(m_State == NotStarted);

    logOp("Start() (key: {})", GetCacheKey().ToString());

    m_State = CreateVertexBuffer;
}

void
ResourceCache::CreateModelOp::Update()
{
    switch(m_State)
    {
        case NotStarted:
            eassert(false, "Start() should have been called before Update()");
            break;

        case CreateVertexBuffer:
            {
                std::vector<std::span<const Vertex>> vertexSpans;

                vertexSpans.reserve(m_ModelSpec.GetMeshSpecs().size());

                for(const auto& meshSpec : m_ModelSpec.GetMeshSpecs())
                {
                    vertexSpans.emplace_back(meshSpec.Vertices);
                }

                auto result = m_ResourceCache->m_GpuDevice->CreateVertexBuffer(vertexSpans);
                if(!result)
                {
                    SetResult(result.error());
                    return;
                }

                m_VertexBuffer = result.value();
            }

            m_State = CreateIndexBuffer;
            break;

        case CreateIndexBuffer:
            {
                std::vector<std::span<const VertexIndex>> indexSpans;

                indexSpans.reserve(m_ModelSpec.GetMeshSpecs().size());

                for(const auto& meshSpec : m_ModelSpec.GetMeshSpecs())
                {
                    indexSpans.emplace_back(meshSpec.Indices);
                }

                auto result = m_ResourceCache->m_GpuDevice->CreateIndexBuffer(indexSpans);
                if(!result)
                {
                    SetResult(result.error());
                    return;
                }

                m_IndexBuffer = result.value();
            }

            m_State = CreateTexturesAndShaders;
            break;

            case CreateTexturesAndShaders:
                m_State = CreatingTexturesAndShaders;

                m_ResourceCache->PushGroup(&m_OpGroup);

                for(const auto& meshSpec : m_ModelSpec.GetMeshSpecs())
                {
                    if(meshSpec.MtlSpec.Albedo.IsValid())
                    {
                        const CacheKey texCacheKey = meshSpec.MtlSpec.Albedo.GetCacheKey();
                        auto result = m_ResourceCache->CreateTextureAsync(texCacheKey,
                            meshSpec.MtlSpec.Albedo);

                        if(!result)
                        {
                            m_FailError = result.error();
                            m_State = Failed;
                            break;
                        }

                        const CacheKey vbCacheKey = meshSpec.MtlSpec.VertexShader.GetCacheKey();
                        result = m_ResourceCache->CreateVertexShaderAsync(vbCacheKey,
                            meshSpec.MtlSpec.VertexShader);

                        if(!result)
                        {
                            m_FailError = result.error();
                            m_State = Failed;
                            break;
                        }
                        const CacheKey fsCacheKey = meshSpec.MtlSpec.FragmentShader.GetCacheKey();
                        result = m_ResourceCache->CreateFragmentShaderAsync(fsCacheKey,
                            meshSpec.MtlSpec.FragmentShader);

                        if(!result)
                        {
                            m_FailError = result.error();
                            m_State = Failed;
                            break;
                        }
                    }
                }

                m_ResourceCache->PopGroup(&m_OpGroup);

                break;

            case CreatingTexturesAndShaders:
                if(m_OpGroup.IsPending())
                {
                    return;
                }

                {
                    auto modelResult = CreateModel();

                    if(!modelResult)
                    {
                        SetResult(modelResult.error());
                        return;
                    }

                    auto modelPtr = m_ResourceCache->m_ModelAllocator.Alloc(std::move(modelResult.value()));

                    SetResult(ModelResource(modelPtr));
                }

                break;

            case Failed:
                //Wait for pending ops to complete
                if(m_OpGroup.IsPending())
                {
                    return;
                }

                SetResult(m_FailError);

                break;

            case Complete:
                // No-op
                break;
    }
}

Result<Model>
ResourceCache::CreateModelOp::CreateModel()
{
    imvector<Mesh>::builder meshes;
    meshes.reserve(m_ModelSpec.GetMeshSpecs().size());

    uint32_t idxOffset = 0, vtxOffset = 0;

    for(const auto& meshSpec : m_ModelSpec.GetMeshSpecs())
    {
        GpuTexture* albedo = nullptr;
        if(meshSpec.MtlSpec.Albedo.IsValid())
        {
            auto albedoResult = m_ResourceCache->GetTexture(meshSpec.MtlSpec.Albedo.GetCacheKey());
            expect(albedoResult, albedoResult.error());
            albedo = albedoResult.value();
        }

        auto vertexShaderResult = m_ResourceCache->GetVertexShader(meshSpec.MtlSpec.VertexShader.GetCacheKey());
        expect(vertexShaderResult, vertexShaderResult.error());

        auto fragShaderResult = m_ResourceCache->GetFragmentShader(meshSpec.MtlSpec.FragmentShader.GetCacheKey());
        expect(fragShaderResult, fragShaderResult.error());

        Material mtl //
            {
                meshSpec.MtlSpec.Color,
                meshSpec.MtlSpec.Metalness,
                meshSpec.MtlSpec.Roughness,
                albedo,
                vertexShaderResult.value(),
                fragShaderResult.value(),
            };

        const uint32_t idxCount = static_cast<uint32_t>(meshSpec.Indices.size());
        const uint32_t vtxCount = static_cast<uint32_t>(meshSpec.Vertices.size());

        // The index and vertex buffers were each created as a single large buffer,
        // so we need to adjust the offsets for each mesh.
        auto ibSubrange = m_IndexBuffer->GetSubrange(idxOffset, idxCount);
        auto vbSubrange = m_VertexBuffer->GetSubrange(vtxOffset, vtxCount);

        meshes.emplace_back(meshSpec.Name,
            vbSubrange,
            ibSubrange,
            idxCount,
            mtl);

        idxOffset += idxCount;
        vtxOffset += vtxCount;
    }

    auto modelResult = Model::Create(meshes.build(),
        m_ModelSpec.GetMeshInstances(),
        m_ModelSpec.GetTransformNodes(),
        m_ResourceCache->m_GpuDevice,
        m_VertexBuffer,
        m_IndexBuffer);

    return modelResult;
}

void
ResourceCache::CreateModelOp::SetResult(Result<ModelResource> result)
{
    if(!result)
    {
        if(m_VertexBuffer)
        {
            auto vbResult = m_ResourceCache->m_GpuDevice->DestroyVertexBuffer(m_VertexBuffer);
            if(!vbResult)
            {
                logError("Failed to destroy vertex buffer: {}", vbResult.error());
            }
        }
        if(m_IndexBuffer)
        {
            auto ibResult = m_ResourceCache->m_GpuDevice->DestroyIndexBuffer(m_IndexBuffer);
            if(!ibResult)
            {
                logError("Failed to destroy index buffer: {}", ibResult.error());
            }
        }
    }

    m_VertexBuffer = nullptr;
    m_IndexBuffer = nullptr;

    if(m_ResourceCache->m_ModelCache.IsPending(GetCacheKey()))
    {
        m_ResourceCache->m_ModelCache.Set(GetCacheKey(), result);
    }

    m_Result = result;

    m_State = Complete;
}

// === ResourceCache::LoadModelOp ===

void
ResourceCache::LoadModelOp::Start()
{
    eassert(m_State == NotStarted);

    logOp("Start() (key: {})", GetCacheKey().ToString());

    m_State = LoadFile;

    /*auto result = FileIo::Fetch(m_Path);

    if(result)
    {
        m_FileFetchToken = result.value();
        m_State = LoadingFile;
    }
    else
    {
        SetResult(result.error());
    }*/
}

void
ResourceCache::LoadModelOp::Update()
{
    switch(m_State)
    {
        case NotStarted:
            eassert(false, "Start() should have been called before Update()");
            break;

        case LoadFile:
        {
            /*constexpr unsigned flags =
                aiProcess_CalcTangentSpace | aiProcess_ImproveCacheLocality | aiProcess_LimitBoneWeights |
                aiProcess_RemoveRedundantMaterials | aiProcess_Triangulate | aiProcess_SortByPType |
                aiProcess_FindDegenerates | aiProcess_FindInvalidData | aiProcess_ConvertToLeftHanded;*/

            constexpr unsigned flags = aiProcess_ConvertToLeftHanded;

            Stopwatch sw;
            sw.Mark();
            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFile(std::string(m_Path), flags);

            logDebug("  Assimp import time: {} ms", static_cast<int>(sw.Elapsed() * 1000.0f));

            if(!scene)
            {
                SetResult(Error(importer.GetErrorString()));
                return;
            }

            m_ModelSpecResult = ProcessScene(scene, m_Path);

            if(!m_ModelSpecResult)
            {
                SetResult(m_ModelSpecResult.error());
                return;
            }

            m_State = LoadingFile;
        }
        break;

        case LoadingFile:
        m_State = CreateModel;
        break;

        /*{
            if(FileIo::IsPending(m_FileFetchToken))
            {
                return;
            }

            logOp("File fetch completed for model (key: {})", GetCacheKey().ToString());

            auto result = FileIo::GetResult(m_FileFetchToken);

            SetResult(LoadModel(result));
            break;
        }*/

        case CreateModel:

            m_CreateModelOp.emplace(m_ResourceCache, GetCacheKey(), m_ModelSpecResult.value());
            m_ResourceCache->Enqueue(&m_CreateModelOp.value());
            m_State = CreatingModel;

            break;

        case CreatingModel:
            // Wait for CreateModelOp to complete
            if(m_CreateModelOp->IsPending())
            {
                return;
            }

            SetResult(m_CreateModelOp->GetResult());

            break;

        case Complete:
            // No-op
            break;
    }
}

void
ResourceCache::LoadModelOp::SetResult(Result<ModelResource> result)
{
    if(m_ResourceCache->m_ModelCache.IsPending(GetCacheKey()))
    {
        m_ResourceCache->m_ModelCache.Set(GetCacheKey(), result);
    }

    m_Result = result;
    m_State = Complete;
}

// === ResourceCache::CreateTextureOp ===

ResourceCache::CreateTextureOp::CreateTextureOp(
    ResourceCache* resourceCache, const CacheKey& cacheKey, const TextureSpec& textureSpec)
    : AsyncOp(cacheKey),
      m_ResourceCache(resourceCache),
      m_TextureSpec(textureSpec)
{
}

ResourceCache::CreateTextureOp::~CreateTextureOp()
{
    if(m_DecodedImageData)
    {
        stbi_image_free(m_DecodedImageData);
        m_DecodedImageData = nullptr;
    }
}

void
ResourceCache::CreateTextureOp::Start()
{
    eassert(m_State == NotStarted);

    logOp("Start() (key: {})", GetCacheKey().ToString());

    auto cacheResult = m_ResourceCache->GetTexture(GetCacheKey());
    if(!everify(!cacheResult))
    {
        logOp("Resource already in cache: {}", GetCacheKey().ToString());
        SetResult(cacheResult);
        return;
    }

    if(!everify(m_TextureSpec.IsValid(), "Texture spec is invalid"))
    {
        SetResult(Error("Texture spec is invalid"));
        return;
    }

    if(RgbaColorf color; m_TextureSpec.TryGetColor(color))
    {
        logOp("Creating texture from color: {}", color.ToHexString());

        auto result = m_ResourceCache->m_GpuDevice->CreateTexture(color, color.ToHexString());
        if(!result)
        {
            SetResult(result.error());
            return;
        }

        SetResult(result);
    }
    else if(imstring path; m_TextureSpec.TryGetPath(path))
    {
        if(path.empty())
        {
            SetResult(Error("Texture source path is empty"));
            return;
        }

        logOp("Creating texture from file: {}", path);

        auto result = FileIo::Fetch(path);

        if(!result)
        {
            SetResult(result.error());
            return;
        }

        m_FileFetchToken = result.value();
        m_State = LoadingFile;
    }
    else
    {
        SetResult(Error("Texture source is not specified"));
    }
}

void
ResourceCache::CreateTextureOp::Update()
{
    switch(m_State)
    {
        case NotStarted:
            eassert(false, "Start() should have been called before Update()");
            break;

        case LoadingFile:
        {
            if(FileIo::IsPending(m_FileFetchToken))
            {
                return;
            }

            auto fetchResult = FileIo::GetResult(m_FileFetchToken);

            if(!fetchResult)
            {
                SetResult(fetchResult.error());
                return;
            }

            m_FetchDataPtr = std::move(fetchResult.value());

            auto job = [this]()
            {
                m_DecodeImageResult = DecodeImage();

                m_DecodeImageComplete.store(true, std::memory_order_release);
            };

            ThreadPool::Enqueue(job);

            m_State = DecodingImage;

            break;
        }

        case DecodingImage:

            if (m_DecodeImageComplete.load(std::memory_order_acquire))
            {
                auto decodeResult = m_DecodeImageResult.value();
                if(!decodeResult)
                {
                    SetResult(decodeResult.error());
                    return;
                }

                auto textureResult = CreateTexture();

                stbi_image_free(m_DecodedImageData);
                m_DecodedImageData = nullptr;

                SetResult(textureResult);
            }
            break;

        case Complete:
            // No-op
            break;
    }
}
void
ResourceCache::CreateTextureOp::SetResult(Result<GpuTexture*> result)
{
    if(m_ResourceCache->m_TextureCache.IsPending(GetCacheKey()))
    {
        m_ResourceCache->m_TextureCache.Set(GetCacheKey(), result);
    }

    m_Result = result;
    m_State = Complete;
}

Result<void>
ResourceCache::CreateTextureOp::DecodeImage()
{
    logOp("Decoding image (key: {})", GetCacheKey().ToString());

    Stopwatch sw;

    /*stbi_info_from_memory(m_FetchDataPtr->Bytes.data(),
        static_cast<int>(m_FetchDataPtr->Bytes.size()),
        &m_DecodedImageWidth,
        &m_DecodedImageHeight,
        &m_DecodedImageChannels);*/

    m_DecodedImageData = stbi_load_from_memory(m_FetchDataPtr->Bytes.data(),
        static_cast<int>(m_FetchDataPtr->Bytes.size()),
        &m_DecodedImageWidth,
        &m_DecodedImageHeight,
        &m_DecodedImageChannels,
        4);

    expect(m_DecodedImageData != nullptr, "Failed to load image from memory: {}", stbi_failure_reason());

    logOp("Image decode completed in {} ms (key: {})",
        static_cast<int>(sw.Elapsed() * 1000.0f),
        GetCacheKey().ToString());

    return ResultOk;
}

Result<GpuTexture*>
ResourceCache::CreateTextureOp::CreateTexture()
{
    logOp("Creating texture (key: {})", GetCacheKey().ToString());

    Stopwatch sw;

    auto result = m_ResourceCache->m_GpuDevice->CreateTexture(static_cast<unsigned>(m_DecodedImageWidth),
        static_cast<unsigned>(m_DecodedImageHeight),
        static_cast<const uint8_t*>(m_DecodedImageData),
        static_cast<unsigned>(m_DecodedImageWidth * 4),
        GetCacheKey().ToString());

    logOp("Texture creation completed in {} ms (key: {})",
        static_cast<int>(sw.Elapsed() * 1000.0f),
        GetCacheKey().ToString());

    return result;
}

// === ResourceCache::CreateVertexShaderOp ===

ResourceCache::CreateVertexShaderOp::CreateVertexShaderOp(
    ResourceCache* resourceCache, const CacheKey& cacheKey, const VertexShaderSpec& shaderSpec)
    : AsyncOp(cacheKey),
      m_ResourceCache(resourceCache),
      m_ShaderSpec(shaderSpec)
{
}

ResourceCache::CreateVertexShaderOp::~CreateVertexShaderOp()
{
}

void
ResourceCache::CreateVertexShaderOp::Start()
{
    eassert(m_State == NotStarted);

    logOp("Start() (key: {})", GetCacheKey().ToString());

    auto cacheResult = m_ResourceCache->GetVertexShader(GetCacheKey());
    if(!everify(!cacheResult))
    {
        logOp("Resource already in cache: {}", GetCacheKey().ToString());
        SetResult(cacheResult);
        return;
    }

    if(!everify(m_ShaderSpec.IsValid(), "Vertex shader spec is invalid"))
    {
        SetResult(Error("Vertex shader spec is invalid"));
        return;
    }

    if(imstring path; m_ShaderSpec.TryGetPath(path))
    {
        if(path.empty())
        {
            SetResult(Error("Vertex shader source path is empty"));
            return;
        }

        path = path + SHADER_EXTENSION;

        logOp("Loading vertex shader from file: {}", path);

        auto result = FileIo::Fetch(path);

        if(!result)
        {
            SetResult(result.error());
            return;
        }

        m_FileFetchToken = result.value();
        m_State = LoadingFile;
    }
    else
    {
        SetResult(Error("Vertex shader source is not specified"));
    }
}

void
ResourceCache::CreateVertexShaderOp::Update()
{
    switch(m_State)
    {
        case NotStarted:
            eassert(false, "Start() should have been called before Update()");
            break;

        case LoadingFile:
        {
            if(FileIo::IsPending(m_FileFetchToken))
            {
                return;
            }

            auto fetchResult = FileIo::GetResult(m_FileFetchToken);

            if(!fetchResult)
            {
                SetResult(fetchResult.error());
                return;
            }

            auto shaderResult = CreateVertexShader(fetchResult.value());

            SetResult(shaderResult);

            m_State = Complete;

            break;
        }

        case Complete:
            // No-op
            break;
    }
}
void
ResourceCache::CreateVertexShaderOp::SetResult(Result<GpuVertexShader*> result)
{
    if(m_ResourceCache->m_VertexShaderCache.IsPending(GetCacheKey()))
    {
        m_ResourceCache->m_VertexShaderCache.Set(GetCacheKey(), result);
    }

    m_Result = result;
    m_State = Complete;
}

Result<GpuVertexShader*>
ResourceCache::CreateVertexShaderOp::CreateVertexShader(const FileIo::FetchDataPtr& fetchData)
{
    logOp("Creating vertex shader (key: {})", GetCacheKey().ToString());

    auto span = std::span<const uint8_t>(fetchData->Bytes.data(), fetchData->Bytes.size());
    auto result = m_ResourceCache->m_GpuDevice->CreateVertexShader(span);

    return result;
}

// === ResourceCache::CreateFragmentShaderOp ===

ResourceCache::CreateFragmentShaderOp::CreateFragmentShaderOp(
    ResourceCache* resourceCache, const CacheKey& cacheKey, const FragmentShaderSpec& shaderSpec)
    : AsyncOp(cacheKey),
      m_ResourceCache(resourceCache),
      m_ShaderSpec(shaderSpec)
{
}

ResourceCache::CreateFragmentShaderOp::~CreateFragmentShaderOp()
{
}

void
ResourceCache::CreateFragmentShaderOp::Start()
{
    eassert(m_State == NotStarted);

    logOp("Start() (key: {})", GetCacheKey().ToString());

    auto cacheResult = m_ResourceCache->GetFragmentShader(GetCacheKey());
    if(!everify(!cacheResult))
    {
        logOp("Resource already in cache: {}", GetCacheKey().ToString());
        SetResult(cacheResult);
        return;
    }

    if(!everify(m_ShaderSpec.IsValid(), "Fragment shader spec is invalid"))
    {
        SetResult(Error("Fragment shader spec is invalid"));
        return;
    }

    if(imstring path; m_ShaderSpec.TryGetPath(path))
    {
        if(path.empty())
        {
            SetResult(Error("Fragment shader source path is empty"));
            return;
        }

        path = path + SHADER_EXTENSION;

        logOp("Loading Fragment shader from file: {}", path);

        auto result = FileIo::Fetch(path);

        if(!result)
        {
            SetResult(result.error());
            return;
        }

        m_FileFetchToken = result.value();
        m_State = LoadingFile;
    }
    else
    {
        SetResult(Error("Fragment shader source is not specified"));
    }
}

void
ResourceCache::CreateFragmentShaderOp::Update()
{
    switch(m_State)
    {
        case NotStarted:
            eassert(false, "Start() should have been called before Update()");
            break;

        case LoadingFile:
        {
            if(FileIo::IsPending(m_FileFetchToken))
            {
                return;
            }

            auto fetchResult = FileIo::GetResult(m_FileFetchToken);

            if(!fetchResult)
            {
                SetResult(fetchResult.error());
                return;
            }

            auto shaderResult = CreateFragmentShader(fetchResult.value());

            SetResult(shaderResult);

            m_State = Complete;

            break;
        }

        case Complete:
            // No-op
            break;
    }
}
void
ResourceCache::CreateFragmentShaderOp::SetResult(Result<GpuFragmentShader*> result)
{
    if(m_ResourceCache->m_FragmentShaderCache.IsPending(GetCacheKey()))
    {
        m_ResourceCache->m_FragmentShaderCache.Set(GetCacheKey(), result);
    }

    m_Result = result;
    m_State = Complete;
}

Result<GpuFragmentShader*>
ResourceCache::CreateFragmentShaderOp::CreateFragmentShader(const FileIo::FetchDataPtr& fetchData)
{
    logOp("Creating fragment shader (key: {})", GetCacheKey().ToString());

    auto span = std::span<const uint8_t>(fetchData->Bytes.data(), fetchData->Bytes.size());
    auto result = m_ResourceCache->m_GpuDevice->CreateFragmentShader(span);

    return result;
}

static TextureProperties
GetTexturePropertiesFromMaterial(const aiMaterial* material,
    const std::filesystem::path& parentPath)
{
    TextureProperties properties;

    aiString texPath;
    aiTextureMapping mapping;
    unsigned uvIndex;
    ai_real blend;
    aiTextureOp op;
    aiTextureMapMode mapmode[2];

    if(material->GetTexture(aiTextureType_BASE_COLOR,
           0,
           &texPath,
           &mapping,
           &uvIndex,
           &blend,
           &op,
           mapmode) == aiReturn_SUCCESS)
    {
        // DO NOT SUBMIT: Temporary warning for testing
        if(mapmode[0] != aiTextureMapMode_Wrap || mapmode[1] != aiTextureMapMode_Wrap)
        {
            logWarn("Base color texture has non-wrapping UV mode");
        }
        properties.Albedo.Path = (parentPath / texPath.C_Str()).string();
        properties.Albedo.UVIndex = uvIndex;
    }
    if(material->GetTexture(aiTextureType_NORMAL_CAMERA,
           0,
           &texPath,
           &mapping,
           &uvIndex,
           &blend,
           &op,
           mapmode) == aiReturn_SUCCESS)
    {
        properties.Normal.Path = (parentPath / texPath.C_Str()).string();
        properties.Normal.UVIndex = uvIndex;
    }
    if(material->GetTexture(aiTextureType_EMISSION_COLOR,
           0,
           &texPath,
           &mapping,
           &uvIndex,
           &blend,
           &op,
           mapmode) == aiReturn_SUCCESS)
    {
        properties.Emission.Path = (parentPath / texPath.C_Str()).string();
        properties.Emission.UVIndex = uvIndex;
    }
    if(material->GetTexture(aiTextureType_METALNESS,
           0,
           &texPath,
           &mapping,
           &uvIndex,
           &blend,
           &op,
           mapmode) == aiReturn_SUCCESS)
    {
        properties.Metalness.Path = (parentPath / texPath.C_Str()).string();
        properties.Metalness.UVIndex = uvIndex;
    }
    if(material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS,
           0,
           &texPath,
           &mapping,
           &uvIndex,
           &blend,
           &op,
           mapmode) == aiReturn_SUCCESS)
    {
        properties.Roughness.Path = (parentPath / texPath.C_Str()).string();
        properties.Roughness.UVIndex = uvIndex;
    }
    if(material->GetTexture(aiTextureType_AMBIENT_OCCLUSION,
           0,
           &texPath,
           &mapping,
           &uvIndex,
           &blend,
           &op,
           mapmode) == aiReturn_SUCCESS)
    {
        properties.AmbientOcclusion.Path = (parentPath / texPath.C_Str()).string();
        properties.AmbientOcclusion.UVIndex = uvIndex;
    }
    return properties;
}

static inline std::string
GetMeshName(const aiMesh* mesh)
{
    return mesh->mName.Empty() ? "<unnamed>" : mesh->mName.C_Str();
}

static void
LogMesh(const aiScene* scene, const SceneMeshId meshId)
{
    const aiMesh* mesh = scene->mMeshes[meshId];
    logDebug("  Mesh {}: {}", meshId, GetMeshName(mesh));
    logDebug("  Vtx: {}, Tri: {}", mesh->mNumVertices, mesh->mNumFaces);
    const aiMaterial* material =
        scene->mMaterials ? scene->mMaterials[mesh->mMaterialIndex] : nullptr;
    if(material)
    {
        logDebug("  Material: \"{}\"", material->GetName().C_Str());
    }
};

static bool
ValidateMesh(const aiScene* scene, const unsigned meshIdx)
{
    const aiMesh* mesh = scene->mMeshes[meshIdx];
    if(!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
    {
        logWarn("Skipping non-triangle mesh");
        LogMesh(scene, meshIdx);
        return false;
    }

    if(mesh->mNumVertices == 0 || mesh->mNumFaces == 0)
    {
        logWarn("Skipping empty mesh");
        LogMesh(scene, meshIdx);
        return false;
    }

    if(!mesh->HasNormals())
    {
        // TODO - generate normals
        logWarn("Mesh has no normals; skipping");
        LogMesh(scene, meshIdx);
        return false;
    }

    return true;
};

static void
CollectMeshSpecs(const aiScene* scene,
    const aiNode* node,
    MeshSpecCollection& outCollection,
    const std::filesystem::path& parentPath)
{
    for(unsigned i = 0; i < node->mNumMeshes; ++i)
    {
        const unsigned meshIdx = node->mMeshes[i];
        if(!ValidateMesh(scene, meshIdx))
        {
            continue;
        }

        const auto meshSpec = CreateMeshSpecFromMesh(scene, meshIdx, parentPath);
        outCollection.MeshIdToSpecIndex[meshIdx] = static_cast<int>(outCollection.MeshSpecs.size());
        outCollection.MeshSpecs.emplace_back(meshSpec);
    }

    for(unsigned i = 0; i < node->mNumChildren; ++i)
    {
        CollectMeshSpecs(scene, node->mChildren[i], outCollection, parentPath);
    }
};

static MaterialSpec
CreateMaterialSpec(const aiMaterial* material, const std::filesystem::path& parentPath)
{
    TextureProperties texProperties;
    ai_real opacity{ 1.0f };
    aiColor3D diffuseColor{ 1.0f, 1.0f, 1.0f };
    if(material)
    {
        if(aiReturn_SUCCESS != material->Get(AI_MATKEY_OPACITY, &opacity, nullptr))
        {
            opacity = 1.0f;
        }

        if(aiReturn_SUCCESS != material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor))
        {
            diffuseColor = aiColor3D{ 1.0f, 1.0f, 1.0f };
        }

        texProperties = GetTexturePropertiesFromMaterial(material, parentPath);
    }
    else
    {
        logWarn("  Mesh has no material");
    }

    const TextureSpec albedo = texProperties.Albedo.Path.empty()
                                   ? MAGENTA_TEXTURE_SPEC
                                   : TextureSpec{ texProperties.Albedo.Path };

    return MaterialSpec{
        .Color{ diffuseColor.r, diffuseColor.g, diffuseColor.b, opacity },
        .Metalness{ 0.0f },
        .Roughness{ 0.0f },
        .Albedo = albedo,
        .VertexShader{ "shaders/Debug/VertexShader.vs", 3 },
        .FragmentShader{ "shaders/Debug/FragmentShader.ps" },
    };
}

static MeshSpec
CreateMeshSpecFromMesh(
    const aiScene* scene, const SceneMeshId meshId, const std::filesystem::path& parentPath)
{
    const aiMesh* mesh = scene->mMeshes[meshId];
    const std::string meshName = GetMeshName(mesh);

    const aiMaterial* material =
        scene->mMaterials ? scene->mMaterials[mesh->mMaterialIndex] : nullptr;

    const MaterialSpec mtlSpec = CreateMaterialSpec(material, parentPath);

    imvector<Vertex>::builder vertices;
    imvector<VertexIndex>::builder indices;
    vertices.reserve(mesh->mNumVertices);
    indices.reserve(mesh->mNumFaces * 3);

    int albedoUvIndex = -1;
    if(material)
    {
        aiGetMaterialInteger(material,
            AI_MATKEY_UVWSRC(aiTextureType_BASE_COLOR, 0),
            &albedoUvIndex);
    }

    // Lambda to get UVs or return zero UVs if not present
    auto getUV = [](const aiMesh* mesh, const int uvIndex, unsigned vtxIdx)
    {
        if(uvIndex < 0 || !mesh->HasTextureCoords(uvIndex))
        {
            return UV2{ 0.0f, 0.0f };
        }

        const aiVector3D& uv = mesh->mTextureCoords[uvIndex][vtxIdx];
        return UV2{ uv.x, uv.y };
    };

    // Transform mesh by node transform
    for(unsigned vtxIdx = 0; vtxIdx < mesh->mNumVertices; ++vtxIdx)
    {
        const aiVector3D& srcVtx = mesh->mVertices[vtxIdx];
        const aiVector3D& srcNorm = mesh->mNormals[vtxIdx];

        Vertex vtx{ .pos = Vec3f{ srcVtx.x, srcVtx.y, srcVtx.z },
            .normal = Vec3f{ srcNorm.x, srcNorm.y, srcNorm.z }.Normalize(),
            .uvs{ getUV(mesh, albedoUvIndex, vtxIdx) } };

        vertices.emplace_back(vtx);
    }

    for(unsigned f = 0; f < mesh->mNumFaces; ++f)
    {
        const aiFace& face = mesh->mFaces[f];

        indices.emplace_back(face.mIndices[0]);
        indices.emplace_back(face.mIndices[1]);
        indices.emplace_back(face.mIndices[2]);
    }

    return MeshSpec{ .Name{ meshName },
        .Vertices{ vertices.build() },
        .Indices{ indices.build() },
        .MtlSpec{ mtlSpec } };
}

static Result<ModelSpec>
ProcessScene(const aiScene* scene, const imstring& filePath)
{
    expect(scene->mNumMeshes > 0, "No meshes in model: {}", filePath);

    const auto absPath = std::filesystem::absolute(filePath.c_str());
    const auto parentPath = absPath.parent_path();

    MeshSpecCollection meshSpecCollection;
    CollectMeshSpecs(scene, scene->mRootNode, meshSpecCollection, parentPath);

    imvector<MeshInstance>::builder meshInstances;
    imvector<TransformNode>::builder transformNodes;

    ProcessNodes(scene->mRootNode,
        -1,
        meshSpecCollection,
        meshInstances,
        transformNodes,
        parentPath);

    const ModelSpec modelSpec //
        {
            meshSpecCollection.MeshSpecs.build(),
            meshInstances.build(),
            transformNodes.build(),
        };

    return modelSpec;
}

static void
ProcessNodes(const aiNode* node,
    const int parentNodeIndex,
    const MeshSpecCollection& meshSpecCollection,
    imvector<MeshInstance>::builder& meshInstances,
    imvector<TransformNode>::builder& transformNodes,
    const std::filesystem::path& parentPath)
{
    logDebug("Processing node {}", node->mName.C_Str());

    if(!node->mNumMeshes)
    {
        if(!node->mNumChildren)
        {
            logWarn("  Node {} has no meshes or children; skipping", node->mName.C_Str());
            return;
        }

        // FIXME(KB) - collapse nodes with no meshes.
        logWarn("  Node {} has no meshes", node->mName.C_Str());
    }

    const aiMatrix4x4& nodeTransform = node->mTransformation;
    const int nodeIndex = static_cast<int>(transformNodes.size());

    transformNodes.emplace_back(TransformNode{ .ParentIndex = parentNodeIndex,
        .Transform = Mat44f{
            // Assimp uses row-major order - transpose to column-major
            nodeTransform.a1,
            nodeTransform.b1,
            nodeTransform.c1,
            nodeTransform.d1,
            nodeTransform.a2,
            nodeTransform.b2,
            nodeTransform.c2,
            nodeTransform.d2,
            nodeTransform.a3,
            nodeTransform.b3,
            nodeTransform.c3,
            nodeTransform.d3,
            nodeTransform.a4,
            nodeTransform.b4,
            nodeTransform.c4,
            nodeTransform.d4,
        } });

    for(unsigned i = 0; i < node->mNumMeshes; ++i)
    {
        const SceneMeshId sceneMeshId = node->mMeshes[i];
        if(!meshSpecCollection.MeshIdToSpecIndex.contains(sceneMeshId))
        {
            logWarn("  Mesh {} not found in mesh spec collection; skipping", sceneMeshId);
            continue;
        }

        const int meshSpecIndex = meshSpecCollection.MeshIdToSpecIndex.at(sceneMeshId);

        const MeshSpec& meshSpec = meshSpecCollection.MeshSpecs[meshSpecIndex];

        logDebug("  Adding mesh instance {}", meshSpec.Name);
        meshInstances.emplace_back(
            MeshInstance{ .MeshIndex = meshSpecIndex, .NodeIndex = nodeIndex });
    }

    for(unsigned i = 0; i < node->mNumChildren; ++i)
    {
        ProcessNodes(node->mChildren[i],
            nodeIndex,
            meshSpecCollection,
            meshInstances,
            transformNodes,
            parentPath);
    }
};