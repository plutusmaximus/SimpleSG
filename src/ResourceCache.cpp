#define __LOGGER_NAME__ "RSRC"

#include "ResourceCache.h"

#include "Logging.h"
#include "scope_exit.h"
#include "ThreadPool.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <filesystem>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

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
    TextureProperty Base;
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
    const Mat44f* coalescingTransform,  // Used to coallesce parent transforms that have no meshes.
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
        auto result = it.second.GetValue();
        if(result)
        {
            auto modelRsrc = result.value();
            m_ModelAllocator.Delete(modelRsrc.Get());
        }
    }

    for(auto it : m_TextureCache)
    {
        eassert(!it.second.IsPending(),
            "Texture cache entry for key {} is still pending during ResourceCache destruction",
            it.first.ToString());

        auto result = it.second.GetValue();
        if(result)
        {
            auto texture = result.value();
            auto dr = m_GpuDevice->DestroyTexture(texture);
            if(!dr)
            {
                logError("Failed to destroy texture: {}", dr.error());
            }
        }
    }

    for(auto it : m_MaterialCache)
    {
        eassert(!it.second.IsPending(),
            "Material cache entry for key {} is still pending during ResourceCache destruction",
            it.first.ToString());

        auto result = it.second.GetValue();
        if(result)
        {
            auto material = result.value();
            auto dr = m_GpuDevice->DestroyMaterial(material);
            if(!dr)
            {
                logError("Failed to destroy material: {}", dr.error());
            }
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
ResourceCache::IsPending<GpuMaterial*>(const CacheKey& cacheKey) const
{
    return m_MaterialCache.IsPending(cacheKey);
}

void
ResourceCache::ProcessPendingOperations()
{
    m_Scheduler.ProcessPendingTasks();
}

Result<ResourceCache::AsyncStatus>
ResourceCache::LoadModelFromFileAsync(const CacheKey& cacheKey, const imstring& filePath)
{
    if(IsPending<ModelResource>(cacheKey))
    {
        logDebug("  Load already pending: {}", cacheKey.ToString());
        auto op = NewOp<WaitOp>(this, cacheKey, &ResourceCache::IsPending<ModelResource>);
        expectv(op, "Failed to allocate WaitOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }
    else if(m_ModelCache.Contains(cacheKey))
    {
        logDebug("  Cache hit: {}", cacheKey.ToString());
    }
    else
    {
        logDebug("  Cache miss: {}", cacheKey.ToString());

        auto op = NewOp<LoadModelOp>(this, cacheKey, filePath);
        expectv(op, "Failed to allocate LoadModelOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }

    return AsyncStatus(this, cacheKey, &ResourceCache::IsPending<ModelResource>);
}

Result<ResourceCache::AsyncStatus>
ResourceCache::CreateModelAsync(const CacheKey& cacheKey, const ModelSpec& modelSpec)
{
    if(IsPending<ModelResource>(cacheKey))
    {
        logDebug("  Creation already pending: {}", cacheKey.ToString());
        auto op = NewOp<WaitOp>(this, cacheKey, &ResourceCache::IsPending<ModelResource>);
        expectv(op, "Failed to allocate WaitOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }
    else if(m_ModelCache.Contains(cacheKey))
    {
        logDebug("  Cache hit: {}", cacheKey.ToString());
    }
    else
    {
        logDebug("  Cache miss: {}", cacheKey.ToString());

        auto op = NewOp<CreateModelOp>(this, cacheKey, modelSpec);
        expectv(op, "Failed to allocate CreateModelOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }

    return AsyncStatus(this, cacheKey, &ResourceCache::IsPending<ModelResource>);
}

Result<ResourceCache::AsyncStatus>
ResourceCache::CreateTextureAsync(const CacheKey& cacheKey, const TextureSpec& textureSpec)
{
    if(IsPending<GpuTexture*>(cacheKey))
    {
        logDebug("  Texture creation already pending: {}", cacheKey.ToString());
        auto op = NewOp<WaitOp>(this, cacheKey, &ResourceCache::IsPending<GpuTexture*>);
        expectv(op, "Failed to allocate WaitOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }
    else if(m_TextureCache.Contains(cacheKey))
    {
        logDebug("  Cache hit: {}", cacheKey.ToString());
    }
    else
    {
        logDebug("  Cache miss: {}", cacheKey.ToString());

        auto op = NewOp<CreateTextureOp>(this, cacheKey, textureSpec);
        expectv(op, "Failed to allocate CreateTextureOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }

    return AsyncStatus(this, cacheKey, &ResourceCache::IsPending<GpuTexture*>);
}

Result<ResourceCache::AsyncStatus>
ResourceCache::CreateMaterialAsync(const CacheKey& cacheKey, const MaterialSpec& materialSpec)
{
    if(IsPending<GpuMaterial*>(cacheKey))
    {
        logDebug("  Material creation already pending: {}", cacheKey.ToString());
        auto op = NewOp<WaitOp>(this, cacheKey, &ResourceCache::IsPending<GpuMaterial*>);
        expectv(op, "Failed to allocate WaitOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }
    else if(m_MaterialCache.Contains(cacheKey))
    {
        logDebug("  Cache hit: {}", cacheKey.ToString());
    }
    else
    {
        logDebug("  Cache miss: {}", cacheKey.ToString());

        auto op = NewOp<CreateMaterialOp>(this, cacheKey, materialSpec);
        expectv(op, "Failed to allocate CreateMaterialOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }

    return AsyncStatus(this, cacheKey, &ResourceCache::IsPending<GpuMaterial*>);
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

Result<GpuMaterial*>
ResourceCache::GetMaterial(const CacheKey& cacheKey) const
{
    Result<GpuMaterial*> result;

    expect(m_MaterialCache.TryGet(cacheKey, result),
        "Material not in cache: {}",
        cacheKey.ToString());

    return result;
}

// private:

#define logOp(fmt, ...) logDebug("  {}: {}", CLASS_NAME, std::format(fmt, __VA_ARGS__))

// === ResourceCache::WaitOp ===

void
ResourceCache::WaitOp::Start()
{
    eassert(m_State == NotStarted);

    logOp("Start() (key: {})", GetCacheKey().ToString());

    m_State = Waiting;
}

void
ResourceCache::WaitOp::Update()
{
    switch(m_State)
    {
        case NotStarted:
            eassert(false, "Start() should have been called before Update()");
            break;

        case Waiting:
            if((m_ResourceCache->*m_IsPendingFunc)(GetCacheKey()))
            {
                return;
            }

            m_State = Complete;
            break;

            case Complete:
                // No-op
                break;
    }
}

// === ResourceCache::CreateModelOp ===

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

    StartDoNotCache();

    m_DoNotCache = false;

    if(!everify(m_ResourceCache->m_ModelCache.TryReserve(GetCacheKey())))
    {
        SetResult(Error("Failed to reserve cache entry for key: {}", GetCacheKey().ToString()));
        return;
    }
}

void
ResourceCache::CreateModelOp::StartDoNotCache()
{
    eassert(m_State == NotStarted);

    logOp("StartDoNotCache() (key: {})", GetCacheKey().ToString());

    m_DoNotCache = true;

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

            m_State = CreateMaterials;
            break;

            case CreateMaterials:

                m_ResourceCache->m_Scheduler.PushGroup(&m_TaskGroup);

                for(const auto& meshSpec : m_ModelSpec.GetMeshSpecs())
                {
                    if(meshSpec.MtlSpec.BaseTexture.IsValid())
                    {
                        auto result =
                            m_ResourceCache->CreateMaterialAsync(meshSpec.MtlSpec.GetCacheKey(),
                                meshSpec.MtlSpec);

                        if(!result)
                        {
                            m_FailError = result.error();
                            m_State = Failed;
                            break;
                        }
                    }
                }

                m_ResourceCache->m_Scheduler.PopGroup(&m_TaskGroup);

                m_State = CreatingMaterials;

                break;

            case CreatingMaterials:
                if(m_TaskGroup.IsPending())
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

                    auto modelPtr = m_ResourceCache->m_ModelAllocator.New(std::move(modelResult.value()));

                    SetResult(ModelResource(modelPtr));
                }

                break;

            case Failed:
                //Wait for pending ops to complete
                if(m_TaskGroup.IsPending())
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

    unsigned idxOffset = 0, vtxOffset = 0;

    for(const auto& meshSpec : m_ModelSpec.GetMeshSpecs())
    {
        auto mtlResult = m_ResourceCache->GetMaterial(meshSpec.MtlSpec.GetCacheKey());
        expect(mtlResult,
            "Material not found in cache for key: {}",
            meshSpec.MtlSpec.GetCacheKey().ToString());

        GpuMaterial* gpuMtl = mtlResult.value();

        const Material mtl(meshSpec.MtlSpec.Constants, gpuMtl->GetBaseTexture());

        const unsigned idxCount = static_cast<unsigned>(meshSpec.Indices.size());
        const unsigned vtxCount = static_cast<unsigned>(meshSpec.Vertices.size());

        meshes.emplace_back(meshSpec.Name,
            m_VertexBuffer,
            m_IndexBuffer,
            idxCount,
            vtxOffset,
            idxOffset,
            mtl,
            gpuMtl);

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

    m_Result = result;

    if(!m_DoNotCache)
    {
        m_ResourceCache->m_ModelCache.Set(GetCacheKey(), result);
    }

    m_State = Complete;
}

// === ResourceCache::LoadModelOp ===

ResourceCache::LoadModelOp::~LoadModelOp()
{
    eassert(!m_CreateModelOp);

    if(m_CreateModelOp)
    {
        m_ResourceCache->DeleteOp(m_CreateModelOp);
    }
}


void
ResourceCache::LoadModelOp::Start()
{
    eassert(m_State == NotStarted);

    logOp("Start() (key: {})", GetCacheKey().ToString());

    if(!everify(m_ResourceCache->m_ModelCache.TryReserve(GetCacheKey())))
    {
        SetResult(Error("Failed to reserve cache entry for key: {}", GetCacheKey().ToString()));
        return;
    }

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

            m_Stopwatch.Mark();

            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFile(std::string(m_Path), flags);

            logDebug("  Assimp import time: {} ms ({})",
                static_cast<int>(m_Stopwatch.Elapsed() * 1000.0f),
                GetCacheKey().ToString());

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

            m_CreateModelOp = m_ResourceCache->NewOp<CreateModelOp>(m_ResourceCache,
                GetCacheKey(),
                m_ModelSpecResult.value());

            if(!m_CreateModelOp)
            {
                SetResult(Error("Failed to allocate CreateModelOp"));
                return;
            }

            // Start the CreateModelOp without caching, since the LoadModelOp is responsible for
            // caching the result when complete.
            m_CreateModelOp->StartDoNotCache();
            m_State = CreatingModel;

            break;

        case CreatingModel:
            // Wait for CreateModelOp to complete
            m_CreateModelOp->Update();
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
    m_ResourceCache->m_ModelCache.Set(GetCacheKey(), result);

    if(m_CreateModelOp)
    {
        m_ResourceCache->DeleteOp(m_CreateModelOp);
        m_CreateModelOp = nullptr;
    }

    logDebug("  Total load time: {} ms ({})",
        static_cast<int>(m_Stopwatch.Elapsed() * 1000.0f),
        GetCacheKey().ToString());

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

    if(!everify(m_TextureSpec.IsValid(), "Texture spec is invalid"))
    {
        SetResult(Error("Texture spec is invalid"));
        return;
    }

    if(!everify(m_ResourceCache->m_TextureCache.TryReserve(GetCacheKey())))
    {
        SetResult(Error("Failed to reserve cache entry for key: {}", GetCacheKey().ToString()));
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

            m_FetchData = std::move(fetchResult.value());

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
    m_ResourceCache->m_TextureCache.Set(GetCacheKey(), result);

    m_State = Complete;
}

Result<void>
ResourceCache::CreateTextureOp::DecodeImage()
{
    logOp("Decoding image (key: {})", GetCacheKey().ToString());

    Stopwatch sw;

    /*stbi_info_from_memory(m_FetchData.Bytes.data(),
        static_cast<int>(m_FetchData.Bytes.size()),
        &m_DecodedImageWidth,
        &m_DecodedImageHeight,
        &m_DecodedImageChannels);*/

    m_DecodedImageData = stbi_load_from_memory(m_FetchData.data(),
        static_cast<int>(m_FetchData.size()),
        &m_DecodedImageWidth,
        &m_DecodedImageHeight,
        &m_DecodedImageChannels,
        4);

    expect(m_DecodedImageData != nullptr, "Failed to load image from memory: {}", stbi_failure_reason());

    logOp("Image decode completed in {} ms (key: {})",
        static_cast<int>(sw.Elapsed() * 1000.0f),
        GetCacheKey().ToString());

    return Result<void>::Success;
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

// === ResourceCache::CreateMaterialOp ===

ResourceCache::CreateMaterialOp::CreateMaterialOp(
    ResourceCache* resourceCache, const CacheKey& cacheKey, const MaterialSpec& materialSpec)
    : AsyncOp(cacheKey),
      m_ResourceCache(resourceCache),
      m_MaterialSpec(materialSpec)
{
}

ResourceCache::CreateMaterialOp::~CreateMaterialOp()
{
}

void
ResourceCache::CreateMaterialOp::Start()
{
    eassert(m_State == NotStarted);

    logOp("Start() (key: {})", GetCacheKey().ToString());

    if(!everify(m_ResourceCache->m_MaterialCache.TryReserve(GetCacheKey())))
    {
        SetResult(Error("Failed to reserve cache entry for key: {}", GetCacheKey().ToString()));
        return;
    }

    const auto& baseTextureSpec = m_MaterialSpec.BaseTexture;

    if(!everify(baseTextureSpec.IsValid(), "Base texture spec is invalid"))
    {
        SetResult(Error("Base texture spec is invalid"));
        return;
    }

    auto result = m_ResourceCache->CreateTextureAsync(baseTextureSpec.GetCacheKey(), baseTextureSpec);

    if(!result)
    {
        SetResult(result.error());
        return;
    }

    m_State = CreatingTexture;
}

void
ResourceCache::CreateMaterialOp::Update()
{
    switch(m_State)
    {
        case NotStarted:
            eassert(false, "Start() should have been called before Update()");
            break;

        case CreatingTexture:
            if(!m_ResourceCache->IsPending<GpuTexture*>(m_MaterialSpec.BaseTexture.GetCacheKey()))
            {
                auto texResult = m_ResourceCache->GetTexture(m_MaterialSpec.BaseTexture.GetCacheKey());
                if(!texResult)
                {
                    SetResult(texResult.error());
                    return;
                }

                auto mtlResult =
                    m_ResourceCache->m_GpuDevice->CreateMaterial(m_MaterialSpec.Constants,
                        texResult.value());

                SetResult(mtlResult);
            }
            break;

        case Complete:
            // No-op
            break;
    }
}
void
ResourceCache::CreateMaterialOp::SetResult(Result<GpuMaterial*> result)
{
    m_ResourceCache->m_MaterialCache.Set(GetCacheKey(), result);

    m_State = Complete;
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
        properties.Base.Path = (parentPath / texPath.C_Str()).string();
        properties.Base.UVIndex = uvIndex;
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

    const TextureSpec baseTextureSpec = texProperties.Base.Path.empty()
                                            ? MAGENTA_TEXTURE_SPEC
                                            : TextureSpec{ texProperties.Base.Path };

    const MaterialConstants materialConstants //
        {
            .Color{ diffuseColor.r, diffuseColor.g, diffuseColor.b, opacity },
            .Metalness{ 0.0f },
            .Roughness{ 0.0f },
        };

    return MaterialSpec(materialConstants, baseTextureSpec);
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

    int baseUvIndex = -1;
    if(material)
    {
        aiGetMaterialInteger(material,
            AI_MATKEY_UVWSRC(aiTextureType_BASE_COLOR, 0),
            &baseUvIndex);
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
            .uvs{ getUV(mesh, baseUvIndex, vtxIdx) } };

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
        nullptr,
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
    const Mat44f* coalescingTransform,
    const MeshSpecCollection& meshSpecCollection,
    imvector<MeshInstance>::builder& meshInstances,
    imvector<TransformNode>::builder& transformNodes,
    const std::filesystem::path& parentPath)
{
    logDebug("Processing node {}", node->mName.C_Str());

    if(!node->mNumMeshes && !node->mNumChildren)
    {
            logWarn("  Node {} has no meshes or children; skipping", node->mName.C_Str());
            return;
    }

    const aiMatrix4x4& nodeTransform = node->mTransformation;

    Mat44f transform//
    {
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
    };

    if(coalescingTransform)
    {
        transform = *coalescingTransform * transform;
    }

    int nodeIndex;
    const Mat44f* curCoalescingTransform;

    if(!node->mNumMeshes)
    {
        nodeIndex = parentNodeIndex;
        // This node has no meshes.  Its transform can be coalesced into
        // the child node transforms.
        curCoalescingTransform = &transform;
    }
    else
    {
        nodeIndex = static_cast<int>(transformNodes.size());
        curCoalescingTransform = nullptr;

        const TransformNode transformNode//
            {
                .ParentIndex = parentNodeIndex,
                .Transform = transform,
            };

        transformNodes.emplace_back(transformNode);

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
    }

    for(unsigned i = 0; i < node->mNumChildren; ++i)
    {
        ProcessNodes(node->mChildren[i],
            nodeIndex,
            curCoalescingTransform,
            meshSpecCollection,
            meshInstances,
            transformNodes,
            parentPath);
    }
};