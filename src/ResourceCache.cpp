#define __LOGGER_NAME__ "RSRC"

#include "ResourceCache.h"

#include "Logging.h"
#include "scope_exit.h"
#include "Stopwatch.h"
#include "ThreadPool.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#define STB_IMAGE_IMPLEMENTATION
#include <filesystem>
#include <stb_image.h>

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

/// @brief Recursively collects meshes from scene nodes.
static void CollectMeshes(
    const aiScene* scene, const aiNode* node, SceneMeshCollection& outCollection);

static MeshSpecCollection CreateMeshSpecCollection(const aiScene* scene,
    const SceneMeshCollection& meshCollection,
    const std::filesystem::path& parentPath);

/// @brief Processes a scene node and its children.
static void ProcessNodes(const aiNode* node,
    const int parentNodeIndex,
    const MeshSpecCollection& meshSpecCollection,
    imvector<MeshInstance>::builder& meshInstances,
    imvector<TransformNode>::builder& transformNodes,
    const std::filesystem::path& parentPath);

ResourceCache::~ResourceCache()
{
    for(auto& vb : m_VertexBuffers)
    {
        // Release vertex buffer resources
        auto result = m_GpuDevice->DestroyVertexBuffer(vb);
        if(!result)
        {
            logError("Failed to destroy vertex buffer: {}", result.error());
        }
    }

    for(auto& ib : m_IndexBuffers)
    {
        // Release index buffer resources
        auto result = m_GpuDevice->DestroyIndexBuffer(ib);
        if(!result)
        {
            logError("Failed to destroy index buffer: {}", result.error());
        }
    }

    for(auto& entry : m_TextureCache)
    {
        if(!entry.Result)
        {
            continue;
        }

        auto result = m_GpuDevice->DestroyTexture(entry.Result.value());
        if(!result)
        {
            logError("Failed to destroy texture: {}", result.error());
        }
    }

    for(auto& entry : m_VertexShaderCache)
    {
        if(!entry.Result)
        {
            continue;
        }

        // Release vertex shader resources
        auto result = m_GpuDevice->DestroyVertexShader(entry.Result.value());
        if(!result)
        {
            logError("Failed to destroy vertex shader: {}", result.error());
        }
    }

    for(auto& entry : m_FragmentShaderCache)
    {
        if(!entry.Result)
        {
            continue;
        }
        // Release fragment shader resources
        auto result = m_GpuDevice->DestroyFragmentShader(entry.Result.value());
        if(!result)
        {
            logError("Failed to destroy fragment shader: {}", result.error());
        }
    }
}

bool
ResourceCache::IsPending(const CacheKey& cacheKey) const
{
    // Check pending operations
    for(AsyncOp* op = m_PendingOps; op != nullptr; op = op->Next())
    {
        if(op->GetCacheKey() == cacheKey)
        {
            return true;
        }
    }

    return false;
}

void
ResourceCache::ProcessPendingOperations()
{
    AsyncOp* op = m_PendingOps;
    while(op)
    {
        AsyncOp* nextOp = op->Next();

        op->Update();

        if(op->IsComplete())
        {
            op->Dequeue(&m_PendingOps);
            FreeOp(op);
        }

        op = nextOp;
    }
}

Result<void>
ResourceCache::LoadModelFromFileAsync(const CacheKey& cacheKey, std::string_view filePath)
{
    // Return existing entry without re-importing
    if(m_ModelCache.Contains(cacheKey))
    {
        logDebug("  Cache hit: {}", cacheKey.ToString());
        return ResultOk;
    }

    if(IsPending(cacheKey))
    {
        logDebug("  Model load already pending: {}", cacheKey.ToString());
        return ResultOk;
    }

    auto op = AllocateOp<LoadModelOp>(this, cacheKey, filePath);
    op->Start();

    op->Enqueue(&m_PendingOps);

    return ResultOk;
}

Result<Model>
ResourceCache::LoadModelFromFile(const CacheKey& cacheKey, std::string_view filePath)
{
    logDebug("Loading model from file: {} (key: {})", filePath, cacheKey.ToString());

    // Return existing entry without re-importing
    Result<Model> modelResult;
    if(m_ModelCache.TryGet(cacheKey, modelResult))
    {
        // TODO(KB) - add a test to confirm cache behavior.

        logDebug("  Cache hit: {}", cacheKey.ToString());
        return modelResult;
    }

    logDebug("  Cache miss: {}", cacheKey.ToString());

    /*constexpr unsigned flags =
        aiProcess_CalcTangentSpace | aiProcess_ImproveCacheLocality | aiProcess_LimitBoneWeights |
        aiProcess_RemoveRedundantMaterials | aiProcess_Triangulate | aiProcess_SortByPType |
        aiProcess_FindDegenerates | aiProcess_FindInvalidData | aiProcess_ConvertToLeftHanded;*/

    constexpr unsigned flags = aiProcess_ConvertToLeftHanded;

    Stopwatch sw;
    sw.Mark();
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(std::string(filePath), flags);

    logDebug("  Assimp import time: {} ms", static_cast<int>(sw.Elapsed() * 1000.0f));

    expect(scene != nullptr, importer.GetErrorString());
    expect(scene->mNumMeshes > 0, "No meshes in model: {}", filePath);

    SceneMeshCollection meshCollection;
    CollectMeshes(scene, scene->mRootNode, meshCollection);

    const auto absPath = std::filesystem::absolute(filePath);
    const auto parentPath = absPath.parent_path();

    auto meshSpecCollection = CreateMeshSpecCollection(scene, meshCollection, parentPath);

    imvector<MeshInstance>::builder meshInstances;
    imvector<TransformNode>::builder transformNodes;

    ProcessNodes(scene->mRootNode,
        -1,
        meshSpecCollection,
        meshInstances,
        transformNodes,
        parentPath);

    const ModelSpec ModelSpec{ meshSpecCollection.MeshSpecs.build(),
        meshInstances.build(),
        transformNodes.build() };

    modelResult = GetOrCreateModel(cacheKey, ModelSpec);

    logDebug("  Total model load time: {} ms", static_cast<int>(sw.Elapsed() * 1000.0f));

    return modelResult;
}

Result<Model>
ResourceCache::LoadModelFromMemory(
    const CacheKey& cacheKey, const std::span<const uint8_t> data, std::string_view filePath)
{
    // Return existing entry without re-importing
    if(Result<Model> modelResult; m_ModelCache.TryGet(cacheKey, modelResult))
    {
        // TODO(KB) - add a test to confirm cache behavior.

        logDebug("  Cache hit: {}", cacheKey.ToString());
        return modelResult;
    }

    constexpr unsigned flags =
        aiProcess_CalcTangentSpace | aiProcess_ImproveCacheLocality | aiProcess_LimitBoneWeights |
        aiProcess_RemoveRedundantMaterials | aiProcess_Triangulate | aiProcess_SortByPType |
        aiProcess_FindDegenerates | aiProcess_FindInvalidData | aiProcess_ConvertToLeftHanded;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFileFromMemory(data.data(), data.size(), flags, nullptr);

    expect(scene != nullptr, importer.GetErrorString());
    expect(scene->mNumMeshes > 0, "No meshes in model: {}", filePath);

    SceneMeshCollection meshCollection;
    CollectMeshes(scene, scene->mRootNode, meshCollection);

    const auto absPath = std::filesystem::absolute(filePath);
    const auto parentPath = absPath.parent_path();

    auto meshSpecCollection = CreateMeshSpecCollection(scene, meshCollection, parentPath);

    imvector<MeshInstance>::builder meshInstances;
    imvector<TransformNode>::builder transformNodes;

    ProcessNodes(scene->mRootNode,
        -1,
        meshSpecCollection,
        meshInstances,
        transformNodes,
        parentPath);

    const ModelSpec ModelSpec{ meshSpecCollection.MeshSpecs.build(),
        meshInstances.build(),
        transformNodes.build() };

    return GetOrCreateModel(cacheKey, ModelSpec);
}

Result<Model>
ResourceCache::GetOrCreateModel(const CacheKey& cacheKey, const ModelSpec& modelSpec)
{
    logDebug("Creating model (key: {})", cacheKey.ToString());

    // Return existing entry without re-importing
    Result<Model> modelResult;
    if(m_ModelCache.TryGet(cacheKey, modelResult))
    {
        // TODO(KB) - add a test to confirm cache behavior.

        logDebug("  Cache hit: {}", cacheKey.ToString());
        return modelResult;
    }

    logDebug("  Cache miss: {}", cacheKey.ToString());

    // Create a single vertex and index buffer for all meshes in the model.

    std::vector<std::span<const Vertex>> vertexSpans;
    std::vector<std::span<const VertexIndex>> indexSpans;

    vertexSpans.reserve(modelSpec.MeshSpecs.size());
    indexSpans.reserve(modelSpec.MeshSpecs.size());

    for(const auto& meshSpec : modelSpec.MeshSpecs)
    {
        vertexSpans.emplace_back(meshSpec.Vertices);
        indexSpans.emplace_back(meshSpec.Indices);
    }

    auto ibResult = m_GpuDevice->CreateIndexBuffer(indexSpans);
    expect(ibResult, ibResult.error());

    auto cleanupIb = scope_exit(
        [&]()
        {
            auto result = m_GpuDevice->DestroyIndexBuffer(ibResult.value());
            if(!result)
            {
                logError("Failed to destroy index buffer: {}", result.error());
            }
        });

    auto vbResult = m_GpuDevice->CreateVertexBuffer(vertexSpans);
    expect(vbResult, vbResult.error());

    auto cleanupVb = scope_exit(
        [&]()
        {
            auto result = m_GpuDevice->DestroyVertexBuffer(vbResult.value());
            if(!result)
            {
                logError("Failed to destroy vertex buffer: {}", result.error());
            }
        });

    auto baseIb = ibResult.value();
    auto baseVb = vbResult.value();

    imvector<Mesh>::builder meshes;
    meshes.reserve(modelSpec.MeshSpecs.size());

    // Load textures into cache
    std::vector<CacheKey> textureCacheKeys;
    for(const auto& meshSpec : modelSpec.MeshSpecs)
    {
        if(meshSpec.MtlSpec.Albedo.IsValid())
        {
            auto result =
                CreateTextureAsync(meshSpec.MtlSpec.Albedo.GetCacheKey(), meshSpec.MtlSpec.Albedo);

            expect(result, result.error());

            textureCacheKeys.push_back(meshSpec.MtlSpec.Albedo.GetCacheKey());
        }
    }

    bool done = false;
    while(!done)
    {
        ProcessPendingOperations();

        done = true;
        for(const auto& textureCacheKey : textureCacheKeys)
        {
            if(IsPending(textureCacheKey))
            {
                done = false;
                break;
            }
        }
    }

    uint32_t idxOffset = 0, vtxOffset = 0;

    for(const auto& meshSpec : modelSpec.MeshSpecs)
    {
        Texture albedo;
        if(meshSpec.MtlSpec.Albedo.IsValid())
        {
            auto albedoResult = GetTexture(meshSpec.MtlSpec.Albedo.GetCacheKey());
            expect(albedoResult, albedoResult.error());
            albedo = albedoResult.value();
        }

        // FIXME - specify number of uniform buffers.
        auto vertexShaderResult = GetOrCreateVertexShader(meshSpec.MtlSpec.VertexShader);
        expect(vertexShaderResult, vertexShaderResult.error());

        // FIXME - specify number of samplers.
        auto fragShaderResult = GetOrCreateFragmentShader(meshSpec.MtlSpec.FragmentShader);
        expect(fragShaderResult, fragShaderResult.error());

        Material mtl{ meshSpec.MtlSpec.Color,
            meshSpec.MtlSpec.Metalness,
            meshSpec.MtlSpec.Roughness,
            albedo,
            vertexShaderResult.value(),
            fragShaderResult.value() };

        const uint32_t idxCount = static_cast<uint32_t>(meshSpec.Indices.size());
        const uint32_t vtxCount = static_cast<uint32_t>(meshSpec.Vertices.size());

        // The index and vertex buffers were each created as a single large buffer,
        // so we need to adjust the offsets for each mesh.
        auto ibSubrangeResult = baseIb.GetSubRange(idxOffset, idxCount);
        expect(ibSubrangeResult, ibSubrangeResult.error());
        auto vbSubrangeResult = baseVb.GetSubRange(vtxOffset, vtxCount);
        expect(vbSubrangeResult, vbSubrangeResult.error());

        auto ibSubrange = ibSubrangeResult.value();
        auto vbSubrange = vbSubrangeResult.value();

        Mesh mesh(meshSpec.Name, vbSubrange, ibSubrange, idxCount, mtl);
        idxOffset += idxCount;
        vtxOffset += vtxCount;

        meshes.emplace_back(mesh);
    }

    modelResult = Model::Create(meshes.build(), modelSpec.MeshInstances, modelSpec.TransformNodes);

    expect(modelResult, modelResult.error());

    expect(m_ModelCache.TryAdd(cacheKey, modelResult),
        "Failed to add model to cache: {}",
        cacheKey.ToString());

    cleanupIb.release();
    cleanupVb.release();

    m_VertexBuffers.push_back(baseVb);
    m_IndexBuffers.push_back(baseIb);

    return modelResult;
}

/// @brief Creates a texture asynchronously if not already created.
Result<void>
ResourceCache::CreateTextureAsync(const CacheKey& cacheKey, const TextureSpec& textureSpec)
{
    // Return existing entry without re-creating
    if(m_TextureCache.Contains(cacheKey))
    {
        logDebug("  Cache hit: {}", cacheKey.ToString());
        return ResultOk;
    }

    if(IsPending(cacheKey))
    {
        logDebug("  Texture creation already pending: {}", cacheKey.ToString());
        return ResultOk;
    }

    auto op = AllocateOp<CreateTextureOp>(this, cacheKey, textureSpec);
    op->Start();
    op->Enqueue(&m_PendingOps);
    return ResultOk;
}

Result<VertexShader>
ResourceCache::GetOrCreateVertexShader(const VertexShaderSpec& shaderSpec)
{
    const CacheKey cacheKey(std::get<0>(shaderSpec.Source));

    Result<VertexShader> shaderResult;
    if(m_VertexShaderCache.TryGet(cacheKey, shaderResult))
    {
        logDebug("  Cache hit: {}", cacheKey.ToString());
        return shaderResult;
    }

    logDebug("  Cache miss: {}", cacheKey.ToString());

    shaderResult = m_GpuDevice->CreateVertexShader(shaderSpec);
    expect(shaderResult, shaderResult.error());

    expect(m_VertexShaderCache.TryAdd(cacheKey, shaderResult),
        "Failed to add vertex shader to cache: {}",
        cacheKey.ToString());
    return shaderResult;
}

Result<FragmentShader>
ResourceCache::GetOrCreateFragmentShader(const FragmentShaderSpec& shaderSpec)
{
    const CacheKey cacheKey(std::get<0>(shaderSpec.Source));

    Result<FragmentShader> shaderResult;
    if(m_FragmentShaderCache.TryGet(cacheKey, shaderResult))
    {
        logDebug("  Cache hit: {}", cacheKey.ToString());
        return shaderResult;
    }

    logDebug("  Cache miss: {}", cacheKey.ToString());
    shaderResult = m_GpuDevice->CreateFragmentShader(shaderSpec);
    expect(shaderResult, shaderResult.error());

    expect(m_FragmentShaderCache.TryAdd(cacheKey, shaderResult),
        "Failed to add fragment shader to cache: {}",
        cacheKey.ToString());
    return shaderResult;
}

Result<Model>
ResourceCache::GetModel(const CacheKey& cacheKey) const
{
    Result<Model> modelResult;
    expect(m_ModelCache.TryGet(cacheKey, modelResult), "Model not found: {}", cacheKey.ToString());
    return modelResult;
}

Result<Texture>
ResourceCache::GetTexture(const CacheKey& cacheKey) const
{
    Result<Texture> textureResult;
    expect(m_TextureCache.TryGet(cacheKey, textureResult),
        "Texture not found: {}",
        cacheKey.ToString());
    return textureResult;
}

Result<VertexShader>
ResourceCache::GetVertexShader(const CacheKey& cacheKey) const
{
    Result<VertexShader> shaderResult;
    expect(m_VertexShaderCache.TryGet(cacheKey, shaderResult),
        "Vertex shader not found: {}",
        cacheKey.ToString());
    return shaderResult;
}

Result<FragmentShader>
ResourceCache::GetFragmentShader(const CacheKey& cacheKey) const
{
    Result<FragmentShader> shaderResult;
    expect(m_FragmentShaderCache.TryGet(cacheKey, shaderResult),
        "Fragment shader not found: {}",
        cacheKey.ToString());
    return shaderResult;
}

// private:

// === ResourceCache::LoadModelOp ===

#define logOp(fmt, ...) logDebug("  {}: {}", CLASS_NAME, std::format(fmt, __VA_ARGS__))

void
ResourceCache::LoadModelOp::Start()
{
    eassert(m_State == NotStarted);

    logOp("Start() (key: {})", GetCacheKey().ToString());

    auto result = FileIo::Fetch(m_Path);

    if(result)
    {
        m_FileFetchToken = result.value();
        m_State = LoadingFile;
    }
    else
    {
        SetResult(result.error());
    }
}

void
ResourceCache::LoadModelOp::Update()
{
    switch(m_State)
    {
        case NotStarted:
            eassert(false);
            break;

        case LoadingFile:
        {
            if(FileIo::IsPending(m_FileFetchToken))
            {
                return;
            }

            logOp("File fetch completed for model (key: {})", GetCacheKey().ToString());

            auto result = FileIo::GetResult(m_FileFetchToken);

            SetResult(LoadModel(result));
            break;
        }
        case Completed:
            // No-op
            break;
    }
}

Result<CacheKey>
ResourceCache::LoadModelOp::LoadModel(const Result<FileIo::FetchDataPtr>& fileData)
{
    if(!fileData)
    {
        return fileData.error();
    }

    logOp("Importing model from memory (key: {})", GetCacheKey().ToString());

    auto result =
        m_ResourceCache->LoadModelFromMemory(GetCacheKey(), fileData.value()->Bytes, m_Path);

    if(!result)
    {
        return result.error();
    }

    return GetCacheKey();
}

// === ResourceCache::CreateTextureOp ===

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
    if(cacheResult)
    {
        logOp("Cache hit: {}", GetCacheKey().ToString());
        SetResult(GetCacheKey());
        return;
    }

    logOp("Cache miss: {}", GetCacheKey().ToString());

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

        AddOrReplaceInCache(result.value());

        SetResult(GetCacheKey());
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
            eassert(false);
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
                auto decodeResult = DecodeImage();

                m_DecodeImageResult = decodeResult;

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

                if(!textureResult)
                {
                    SetResult(textureResult.error());
                    return;
                }

                AddOrReplaceInCache(textureResult.value());

                SetResult(GetCacheKey());

                stbi_image_free(m_DecodedImageData);
                m_DecodedImageData = nullptr;
            }
            break;

        case Completed:
            // No-op
            break;
    }
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

Result<Texture>
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

void
ResourceCache::CreateTextureOp::AddOrReplaceInCache(const Texture& texture)
{
    logOp("Adding texture to cache (key: {})", GetCacheKey().ToString());

    m_ResourceCache->m_TextureCache.AddOrReplace(GetCacheKey(), texture);
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
CollectMeshes(const aiScene* scene, const aiNode* node, SceneMeshCollection& outCollection)
{
    for(unsigned i = 0; i < node->mNumMeshes; ++i)
    {
        const unsigned meshIdx = node->mMeshes[i];
        if(!ValidateMesh(scene, meshIdx))
        {
            continue;
        }

        const aiMesh* mesh = scene->mMeshes[meshIdx];

        outCollection[meshIdx] = mesh;
    }

    for(unsigned i = 0; i < node->mNumChildren; ++i)
    {
        CollectMeshes(scene, node->mChildren[i], outCollection);
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
        .VertexShader{ "shaders/Debug/VertexShader", 3 },
        .FragmentShader{ "shaders/Debug/FragmentShader" },
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

static MeshSpecCollection
CreateMeshSpecCollection(const aiScene* scene,
    const SceneMeshCollection& meshCollection,
    const std::filesystem::path& parentPath)
{
    MeshSpecCollection meshSpecCollection;

    for(const auto& [meshId, mesh] : meshCollection)
    {
        MeshSpec spec = CreateMeshSpecFromMesh(scene, meshId, parentPath);

        const int specIndex = static_cast<int>(meshSpecCollection.MeshSpecs.size());
        meshSpecCollection.MeshSpecs.emplace_back(std::move(spec));
        meshSpecCollection.MeshIdToSpecIndex[meshId] = specIndex;
    }

    return meshSpecCollection;
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