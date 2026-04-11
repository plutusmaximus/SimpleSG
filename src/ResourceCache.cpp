#define __LOGGER_NAME__ "RSRC"

#include "ResourceCache.h"

#include "CgltfModelLoader.h"
#include "Log.h"
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
    const TransformIndex parentNodeIndex,
    const MeshSpecCollection& meshSpecCollection,
    imvector<TransformIndex>::builder& meshToTransformMapping,
    imvector<TransformNode>::builder& transformNodes,
    const std::filesystem::path& parentPath);

/// @brief Creates a model specification from a scene.
static Result<ModelSpec>
ProcessScene(const aiScene* scene, const imstring& filePath);

ResourceCache::~ResourceCache()
{
    for(auto& it : m_ModelCache)
    {
        MLG_ASSERT(!it.second.IsPending(),
            "Model cache entry for key {} is still pending during ResourceCache destruction",
            it.first.ToString());

        // Release model resources
        auto result = it.second.GetValue();
        if(result)
        {
            m_ModelAllocator.Delete(result->Get());
        }
    }

    for(auto it : m_TextureCache)
    {
        MLG_ASSERT(!it.second.IsPending(),
            "Texture cache entry for key {} is still pending during ResourceCache destruction",
            it.first.ToString());

        auto result = it.second.GetValue();
        if(result)
        {
            auto dr = m_GpuDevice->DestroyTexture(*result);
            if(!dr)
            {
                MLG_ERROR("Failed to destroy texture");
            }
        }
    }

    for(auto it : m_MaterialCache)
    {
        MLG_ASSERT(!it.second.IsPending(),
            "Material cache entry for key {} is still pending during ResourceCache destruction",
            it.first.ToString());

        auto result = it.second.GetValue();
        if(result)
        {
            auto dr = m_GpuDevice->DestroyMaterial(*result);
            if(!dr)
            {
                MLG_ERROR("Failed to destroy material");
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
        MLG_DEBUG("  Load already pending: {}", cacheKey.ToString());
        auto op = NewOp<WaitOp>(this, cacheKey, &ResourceCache::IsPending<ModelResource>);
        MLG_CHECKV(op, "Failed to allocate WaitOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }
    else if(m_ModelCache.Contains(cacheKey))
    {
        MLG_DEBUG("  Cache hit: {}", cacheKey.ToString());
    }
    else
    {
        MLG_DEBUG("  Cache miss: {}", cacheKey.ToString());

        auto op = NewOp<LoadModelOp>(this, cacheKey, filePath);
        MLG_CHECKV(op, "Failed to allocate LoadModelOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }

    return AsyncStatus(this, cacheKey, &ResourceCache::IsPending<ModelResource>);
}

Result<ResourceCache::AsyncStatus>
ResourceCache::CreateModelAsync(const CacheKey& cacheKey, const ModelSpec& modelSpec)
{
    if(IsPending<ModelResource>(cacheKey))
    {
        MLG_DEBUG("  Creation already pending: {}", cacheKey.ToString());
        auto op = NewOp<WaitOp>(this, cacheKey, &ResourceCache::IsPending<ModelResource>);
        MLG_CHECKV(op, "Failed to allocate WaitOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }
    else if(m_ModelCache.Contains(cacheKey))
    {
        MLG_DEBUG("  Cache hit: {}", cacheKey.ToString());
    }
    else
    {
        MLG_DEBUG("  Cache miss: {}", cacheKey.ToString());

        auto op = NewOp<CreateModelOp>(this, cacheKey, modelSpec);
        MLG_CHECKV(op, "Failed to allocate CreateModelOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }

    return AsyncStatus(this, cacheKey, &ResourceCache::IsPending<ModelResource>);
}

Result<ResourceCache::AsyncStatus>
ResourceCache::CreateTextureAsync(const CacheKey& cacheKey, const TextureSpec& textureSpec)
{
    if(IsPending<GpuTexture*>(cacheKey))
    {
        MLG_DEBUG("  Texture creation already pending: {}", cacheKey.ToString());
        auto op = NewOp<WaitOp>(this, cacheKey, &ResourceCache::IsPending<GpuTexture*>);
        MLG_CHECKV(op, "Failed to allocate WaitOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }
    else if(m_TextureCache.Contains(cacheKey))
    {
        MLG_DEBUG("  Cache hit: {}", cacheKey.ToString());
    }
    else
    {
        MLG_DEBUG("  Cache miss: {}", cacheKey.ToString());

        auto op = NewOp<CreateTextureOp>(this, cacheKey, textureSpec);
        MLG_CHECKV(op, "Failed to allocate CreateTextureOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }

    return AsyncStatus(this, cacheKey, &ResourceCache::IsPending<GpuTexture*>);
}

Result<ResourceCache::AsyncStatus>
ResourceCache::CreateMaterialAsync(const CacheKey& cacheKey, const MaterialSpec& materialSpec)
{
    if(IsPending<GpuMaterial*>(cacheKey))
    {
        MLG_DEBUG("  Material creation already pending: {}", cacheKey.ToString());
        auto op = NewOp<WaitOp>(this, cacheKey, &ResourceCache::IsPending<GpuMaterial*>);
        MLG_CHECKV(op, "Failed to allocate WaitOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }
    else if(m_MaterialCache.Contains(cacheKey))
    {
        MLG_DEBUG("  Cache hit: {}", cacheKey.ToString());
    }
    else
    {
        MLG_DEBUG("  Cache miss: {}", cacheKey.ToString());

        auto op = NewOp<CreateMaterialOp>(this, cacheKey, materialSpec);
        MLG_CHECKV(op, "Failed to allocate CreateMaterialOp for key: {}", cacheKey.ToString());
        m_Scheduler.Enqueue(op);
    }

    return AsyncStatus(this, cacheKey, &ResourceCache::IsPending<GpuMaterial*>);
}

Result<ModelResource>
ResourceCache::GetModel(const CacheKey& cacheKey) const
{
    Result<ModelResource> result;

    MLG_CHECK(m_ModelCache.TryGet(cacheKey, result), "Model not in cache: {}", cacheKey.ToString());

    return result;
}

Result<GpuTexture*>
ResourceCache::GetTexture(const CacheKey& cacheKey) const
{
    Result<GpuTexture*> result;

    MLG_CHECK(m_TextureCache.TryGet(cacheKey, result), "Texture not in cache: {}", cacheKey.ToString());

    return result;
}

Result<GpuMaterial*>
ResourceCache::GetMaterial(const CacheKey& cacheKey) const
{
    Result<GpuMaterial*> result;

    MLG_CHECK(m_MaterialCache.TryGet(cacheKey, result),
        "Material not in cache: {}",
        cacheKey.ToString());

    return result;
}

// private:

#define logOp(fmt, ...) MLG_DEBUG("  {}: {}", CLASS_NAME, std::format(fmt, __VA_ARGS__))

// === ResourceCache::WaitOp ===

void
ResourceCache::WaitOp::Start()
{
    MLG_ASSERT(m_State == NotStarted);

    logOp("Start() (key: {})", GetCacheKey().ToString());

    m_State = Waiting;
}

void
ResourceCache::WaitOp::Update()
{
    switch(m_State)
    {
        case NotStarted:
            MLG_ASSERT(false, "Start() should have been called before Update()");
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
    MLG_ASSERT(!m_VertexBuffer);
    MLG_ASSERT(!m_IndexBuffer);

    if(m_VertexBuffer)
    {
        auto result = m_ResourceCache->m_GpuDevice->DestroyVertexBuffer(m_VertexBuffer);
        if(!result)
        {
            MLG_ERROR("Failed to destroy vertex buffer");
        }
    }

    if(m_IndexBuffer)
    {
        auto result = m_ResourceCache->m_GpuDevice->DestroyIndexBuffer(m_IndexBuffer);
        if(!result)
        {
            MLG_ERROR("Failed to destroy index buffer");
        }
    }
}

void
ResourceCache::CreateModelOp::Start()
{
    MLG_ASSERT(m_State == NotStarted);

    StartDoNotCache();

    m_DoNotCache = false;

    if(!MLG_VERIFY(m_ResourceCache->m_ModelCache.TryReserve(GetCacheKey())))
    {
        MLG_ERROR("Failed to reserve cache entry for key: {}", GetCacheKey().ToString());
        SetResult(Result<>::Fail);
        return;
    }
}

void
ResourceCache::CreateModelOp::StartDoNotCache()
{
    MLG_ASSERT(m_State == NotStarted);

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
            MLG_ASSERT(false, "Start() should have been called before Update()");
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
                    SetResult(Result<>::Fail);
                    return;
                }

                m_VertexBuffer = *result;
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
                    SetResult(Result<>::Fail);
                    return;
                }

                m_IndexBuffer = *result;
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
                        SetResult(Result<>::Fail);
                        return;
                    }

                    auto modelPtr = m_ResourceCache->m_ModelAllocator.New(std::move(*modelResult));

                    SetResult(ModelResource(modelPtr));
                }

                break;

            case Failed:
                //Wait for pending ops to complete
                if(m_TaskGroup.IsPending())
                {
                    return;
                }

                SetResult(Result<>::Fail);

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
        MLG_CHECK(mtlResult,
            "Material not found in cache for key: {}",
            meshSpec.MtlSpec.GetCacheKey().ToString());

        GpuMaterial* gpuMtl = *mtlResult;

        const unsigned idxCount = static_cast<unsigned>(meshSpec.Indices.size());
        const unsigned vtxCount = static_cast<unsigned>(meshSpec.Vertices.size());

        meshes.emplace_back(meshSpec.Name,
            idxCount,
            vtxOffset,
            idxOffset,
            gpuMtl);

        idxOffset += idxCount;
        vtxOffset += vtxCount;
    }

    const auto& mapping = m_ModelSpec.GetMeshToTransformMapping();
    const size_t sizeofMappingBuffer = mapping.size() * sizeof(TransformIndex);
    auto meshToTransformMappingBuffer = m_ResourceCache->m_GpuDevice->CreateStorageBuffer(sizeofMappingBuffer);
    MLG_CHECK(meshToTransformMappingBuffer);
    const std::span<const uint8_t> mappingSpan(reinterpret_cast<const uint8_t*>(mapping.data()), sizeofMappingBuffer);
    (*meshToTransformMappingBuffer)->WriteBuffer(mappingSpan);

    const auto& transformNodes = m_ModelSpec.GetTransformNodes();
    std::vector<Mat44f> transforms;
    transforms.reserve(transformNodes.size());
    for(const auto& node : transformNodes)
    {
        if(node.ParentIndex != kInvalidTransformIndex)
        {
            transforms.emplace_back(
                transforms[node.ParentIndex].Mul(node.Transform));
        }
        else
        {
            transforms.emplace_back(node.Transform);
        }
    }

    const size_t sizeofTransformBuffer = transforms.size() * sizeof(Mat44f);
    auto transformBuffer = m_ResourceCache->m_GpuDevice->CreateStorageBuffer(sizeofTransformBuffer);
    MLG_CHECK(transformBuffer);
    const std::span<const uint8_t> transformSpan(reinterpret_cast<const uint8_t*>(transforms.data()), sizeofTransformBuffer);
    (*transformBuffer)->WriteBuffer(transformSpan);

    class DrawIndirectBufferParams
    {
    public:
        uint32_t IndexCount;
        uint32_t InstanceCount;
        uint32_t FirstIndex;
        uint32_t BaseVertex;
        uint32_t FirstInstance;
    };

    std::vector<DrawIndirectBufferParams> drawIndirectBuffers;

    drawIndirectBuffers.reserve(meshes.size());

    for(size_t i = 0; i < meshes.size(); ++i)
    {
        const Mesh* mesh = &meshes[i];

        drawIndirectBuffers.emplace_back(DrawIndirectBufferParams //
            {
                .IndexCount = mesh->GetIndexCount(),
                .InstanceCount = 1,
                .FirstIndex = mesh->GetIndexOffset(),
                .BaseVertex = mesh->GetVertexOffset(),
                .FirstInstance = static_cast<uint32_t>(i),
            });
    }

    auto drawIndirectBuffer = m_ResourceCache->m_GpuDevice->CreateDrawIndirectBuffer(
        drawIndirectBuffers.size() * sizeof(DrawIndirectBufferParams));
    MLG_CHECK(drawIndirectBuffer);
    const std::span<const uint8_t> drawIndirectSpan(
        reinterpret_cast<const uint8_t*>(drawIndirectBuffers.data()),
        drawIndirectBuffers.size() * sizeof(DrawIndirectBufferParams));
    (*drawIndirectBuffer)->WriteBuffer(drawIndirectSpan);

    auto modelResult = Model::Create(meshes.build(),
        m_ResourceCache->m_GpuDevice,
        *transformBuffer,
        *meshToTransformMappingBuffer,
        *drawIndirectBuffer,
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
                MLG_ERROR("Failed to destroy vertex buffer");
            }
        }
        if(m_IndexBuffer)
        {
            auto ibResult = m_ResourceCache->m_GpuDevice->DestroyIndexBuffer(m_IndexBuffer);
            if(!ibResult)
            {
                MLG_ERROR("Failed to destroy index buffer");
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
    MLG_ASSERT(!m_CreateModelOp);

    if(m_CreateModelOp)
    {
        m_ResourceCache->DeleteOp(m_CreateModelOp);
    }
}

void
ResourceCache::LoadModelOp::Start()
{
    MLG_ASSERT(m_State == NotStarted);

    logOp("Start() (key: {})", GetCacheKey().ToString());

    if(!MLG_VERIFY(m_ResourceCache->m_ModelCache.TryReserve(GetCacheKey())))
    {
        MLG_ERROR("Failed to reserve cache entry for key: {}", GetCacheKey().ToString());
        SetResult(Result<>::Fail);
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
            MLG_ASSERT(false, "Start() should have been called before Update()");
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

            MLG_DEBUG("  Assimp import time: {} ms ({})",
                static_cast<int>(m_Stopwatch.Elapsed() * 1000.0f),
                GetCacheKey().ToString());

            if(!scene)
            {
                MLG_ERROR("Failed to load model file: {} ({})", m_Path, importer.GetErrorString());
                SetResult(Result<>::Fail);
                return;
            }

            m_ModelSpecResult = ProcessScene(scene, m_Path);

            if(!m_ModelSpecResult)
            {
                SetResult(Result<>::Fail);
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
                *m_ModelSpecResult);

            if(!m_CreateModelOp)
            {
                MLG_ERROR("Failed to allocate CreateModelOp");
                SetResult(Result<>::Fail);
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

    MLG_DEBUG("  Total load time: {} ms ({})",
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
    MLG_ASSERT(m_State == NotStarted);

    logOp("Start() (key: {})", GetCacheKey().ToString());

    if(!MLG_VERIFY(m_TextureSpec.IsValid(), "Texture spec is invalid"))
    {
        MLG_ERROR("Texture spec is invalid");
        SetResult(Result<>::Fail);
        return;
    }

    if(!MLG_VERIFY(m_ResourceCache->m_TextureCache.TryReserve(GetCacheKey())))
    {
        MLG_ERROR("Failed to reserve cache entry for key: {}", GetCacheKey().ToString());
        SetResult(Result<>::Fail);
        return;
    }

    if(RgbaColorf color; m_TextureSpec.TryGetColor(color))
    {
        logOp("Creating texture from color: {}", color.ToHexString());

        auto result = m_ResourceCache->m_GpuDevice->CreateTexture(color, color.ToHexString());
        if(!result)
        {
            SetResult(Result<>::Fail);
            return;
        }

        SetResult(result);
    }
    else if(imstring path; m_TextureSpec.TryGetPath(path))
    {
        if(path.empty())
        {
            MLG_ERROR("Texture source path is empty");
            SetResult(Result<>::Fail);
            return;
        }

        logOp("Creating texture from file: {}", path);

        auto result = FileIo::Fetch(path);

        if(!result)
        {
            SetResult(Result<>::Fail);

            return;
        }

        m_FileFetchToken = *result;
        m_State = LoadingFile;
    }
    else
    {
        MLG_ERROR("Texture source is not specified");
        SetResult(Result<>::Fail);
    }
}

void
ResourceCache::CreateTextureOp::Update()
{
    switch(m_State)
    {
        case NotStarted:
            MLG_ASSERT(false, "Start() should have been called before Update()");
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
                SetResult(Result<>::Fail);
                return;
            }

            m_FetchData = std::move(*fetchResult);

            auto job = [](void* userData)
            {
                auto op = reinterpret_cast<ResourceCache::CreateTextureOp*>(userData);
                op->m_DecodeImageResult = op->DecodeImage();

                op->m_DecodeImageComplete.store(true, std::memory_order_release);
            };

            ThreadPool::Enqueue(job, this);

            m_State = DecodingImage;

            break;
        }

        case DecodingImage:

            if (m_DecodeImageComplete.load(std::memory_order_acquire))
            {
                auto decodeResult = m_DecodeImageResult.value();
                if(!decodeResult)
                {
                    SetResult(Result<>::Fail);
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

Result<>
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

    MLG_CHECK(m_DecodedImageData != nullptr, "Failed to load image from memory: {}", stbi_failure_reason());

    logOp("Image decode completed in {} ms (key: {})",
        static_cast<int>(sw.Elapsed() * 1000.0f),
        GetCacheKey().ToString());

    return Result<>::Ok;
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
    MLG_ASSERT(m_State == NotStarted);

    logOp("Start() (key: {})", GetCacheKey().ToString());

    if(!MLG_VERIFY(m_ResourceCache->m_MaterialCache.TryReserve(GetCacheKey())))
    {
        MLG_ERROR("Failed to reserve cache entry for key: {}", GetCacheKey().ToString());
        SetResult(Result<>::Fail);
        return;
    }

    const auto& baseTextureSpec = m_MaterialSpec.BaseTexture;

    if(!MLG_VERIFY(baseTextureSpec.IsValid(), "Base texture spec is invalid"))
    {
        MLG_ERROR("Base texture spec is invalid");
        SetResult(Result<>::Fail);
        return;
    }

    auto result = m_ResourceCache->CreateTextureAsync(baseTextureSpec.GetCacheKey(), baseTextureSpec);

    if(!result)
    {
        SetResult(Result<>::Fail);
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
            MLG_ASSERT(false, "Start() should have been called before Update()");
            break;

        case CreatingTexture:
            if(!m_ResourceCache->IsPending<GpuTexture*>(m_MaterialSpec.BaseTexture.GetCacheKey()))
            {
                auto texResult = m_ResourceCache->GetTexture(m_MaterialSpec.BaseTexture.GetCacheKey());
                if(!texResult)
                {
                    SetResult(Result<>::Fail);
                    return;
                }

                auto mtlResult =
                    m_ResourceCache->m_GpuDevice->CreateMaterial(m_MaterialSpec.Constants,
                        *texResult);

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
            MLG_WARN("Base color texture has non-wrapping UV mode");
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
    MLG_DEBUG("  Mesh {}: {}", meshId, GetMeshName(mesh));
    MLG_DEBUG("  Vtx: {}, Tri: {}", mesh->mNumVertices, mesh->mNumFaces);
    const aiMaterial* material =
        scene->mMaterials ? scene->mMaterials[mesh->mMaterialIndex] : nullptr;
    if(material)
    {
        MLG_DEBUG("  Material: \"{}\"", material->GetName().C_Str());
    }
};

static bool
ValidateMesh(const aiScene* scene, const unsigned meshIdx)
{
    const aiMesh* mesh = scene->mMeshes[meshIdx];
    if(!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE))
    {
        MLG_WARN("Skipping non-triangle mesh");
        LogMesh(scene, meshIdx);
        return false;
    }

    if(mesh->mNumVertices == 0 || mesh->mNumFaces == 0)
    {
        MLG_WARN("Skipping empty mesh");
        LogMesh(scene, meshIdx);
        return false;
    }

    if(!mesh->HasNormals())
    {
        // TODO - generate normals
        MLG_WARN("Mesh has no normals; skipping");
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
        MLG_WARN("  Mesh has no material");
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
    MLG_CHECK(scene->mNumMeshes > 0, "No meshes in model: {}", filePath);

    const auto absPath = std::filesystem::absolute(filePath.c_str());
    const auto parentPath = absPath.parent_path();

    MeshSpecCollection meshSpecCollection;
    CollectMeshSpecs(scene, scene->mRootNode, meshSpecCollection, parentPath);

    imvector<TransformIndex>::builder meshToTransformMapping;
    imvector<TransformNode>::builder transformNodes;

    ProcessNodes(scene->mRootNode,
        kInvalidTransformIndex,
        meshSpecCollection,
        meshToTransformMapping,
        transformNodes,
        parentPath);

    const ModelSpec modelSpec //
        {
            meshSpecCollection.MeshSpecs.build(),
            meshToTransformMapping.build(),
            transformNodes.build(),
        };

    return modelSpec;
}

static void
ProcessNodes(const aiNode* node,
    const TransformIndex parentNodeIndex,
    const MeshSpecCollection& meshSpecCollection,
    imvector<TransformIndex>::builder& meshToTransformMapping,
    imvector<TransformNode>::builder& transformNodes,
    const std::filesystem::path& parentPath)
{
    MLG_DEBUG("Processing node {}", node->mName.C_Str());

    if(!node->mNumMeshes && !node->mNumChildren)
    {
            MLG_WARN("  Node {} has no meshes or children; skipping", node->mName.C_Str());
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

    int nodeIndex = static_cast<TransformIndex>(transformNodes.size());

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
            MLG_WARN("  Mesh {} not found in mesh spec collection; skipping", sceneMeshId);
            continue;
        }

        const int meshSpecIndex = meshSpecCollection.MeshIdToSpecIndex.at(sceneMeshId);

        const MeshSpec& meshSpec = meshSpecCollection.MeshSpecs[meshSpecIndex];

        MLG_DEBUG("  Adding mesh instance {}", meshSpec.Name);
        meshToTransformMapping.emplace_back(nodeIndex);
    }

    for(unsigned i = 0; i < node->mNumChildren; ++i)
    {
        ProcessNodes(node->mChildren[i],
            nodeIndex,
            meshSpecCollection,
            meshToTransformMapping,
            transformNodes,
            parentPath);
    }
};