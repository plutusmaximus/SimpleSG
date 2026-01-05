#include "ModelCatalog.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <filesystem>

static constexpr const char* WHITE_TEXTURE_KEY = "$white";
static constexpr const char* MAGENTA_TEXTURE_KEY = "$magenta";

static const TextureSpec WHITE_TEXTURE_SPEC(
    WHITE_TEXTURE_KEY,
    RgbaColorf{ 1.0f, 1.0f, 1.0f, 1.0f });

static const TextureSpec MAGENTA_TEXTURE_SPEC(
    MAGENTA_TEXTURE_KEY,
    RgbaColorf{ 1.0f, 0.0f, 1.0f, 1.0f });

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
    std::vector<MeshSpec> MeshSpecs;
    std::unordered_map<SceneMeshId, int> MeshIdToSpecIndex;
};

/// @brief Retrieves texture properties from a given material.
static TextureProperties GetTexturePropertiesFromMaterial(
    const aiMaterial* material,
    const std::filesystem::path& parentPath);

/// @brief Retrieves the name of a mesh.
static inline std::string GetMeshName(const aiMesh* mesh);

/// @brief Logs information about a mesh.
static void LogMesh(const aiScene* scene, const unsigned meshIdx);

/// @brief Validates a mesh in a scene.
static bool ValidateMesh(const aiScene* scene, const unsigned meshIdx);

/// @brief Recursively collects meshes from scene nodes.
static void CollectMeshes(const aiScene* scene, const aiNode* node, SceneMeshCollection& outCollection);

static MeshSpecCollection CreateMeshSpecCollection(
    const aiScene* scene,
    const SceneMeshCollection& meshCollection,
    const std::filesystem::path& parentPath);

/// @brief Processes a scene node and its children.
static void ProcessNodes(
        const aiNode* node,
        const int parentNodeIndex,
        const MeshSpecCollection& meshSpecCollection,
        std::vector<MeshInstance>& meshInstances,
        std::vector<TransformNode>& transformNodes,
        const std::filesystem::path& parentPath);

Result<RefPtr<Model>>
ModelCatalog::LoadModelFromFile(const CacheKey& cacheKey, const std::string& filePath)
{
    logDebug("Loading model from file: {} (key: {})", filePath, cacheKey.ToString());

    RefPtr<Model> model;

    // Return existing entry without re-importing
    if(m_ModelCache.TryGet(cacheKey, model))
    {
        //TODO(KB) - add a test to confirm cache behavior.

        logDebug("  Model already exists in cache (key: {})", cacheKey.ToString());
        return model;
    }

    constexpr unsigned flags =
        aiProcess_CalcTangentSpace
        //| aiProcess_GenSmoothNormals
        | aiProcess_ImproveCacheLocality
        | aiProcess_LimitBoneWeights
        | aiProcess_RemoveRedundantMaterials
        //| aiProcess_SplitLargeMeshes
        | aiProcess_Triangulate
        //| aiProcess_GenUVCoords
        | aiProcess_SortByPType
        | aiProcess_FindDegenerates
        //| aiProcess_JoinIdenticalVertices
        | aiProcess_FindInvalidData
        | aiProcess_ConvertToLeftHanded
        ;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filePath, flags);

    expect(scene != nullptr, importer.GetErrorString());
    expect(scene->mNumMeshes > 0, "No meshes in model: {}", filePath);

    SceneMeshCollection meshCollection;
    CollectMeshes(scene, scene->mRootNode, meshCollection);
    
    const auto absPath = std::filesystem::absolute(filePath);    
    const auto parentPath = absPath.parent_path();

    auto meshSpecCollection = CreateMeshSpecCollection(
        scene,
        meshCollection,
        parentPath);

    std::vector<MeshInstance> meshInstances;
    std::vector<TransformNode> transformNodes;

    ProcessNodes(
        scene->mRootNode,
        -1,
        meshSpecCollection,
        meshInstances,
        transformNodes,
        parentPath);

    const ModelSpec ModelSpec
    {
        std::move(meshSpecCollection.MeshSpecs),
        std::move(meshInstances),
        std::move(transformNodes)
    };

    return CreateModel(cacheKey, ModelSpec);
}

Result<RefPtr<Model>>
ModelCatalog::CreateModel(const CacheKey& cacheKey, const ModelSpec& modelSpec)
{
    logDebug("Creating model (key: {})", cacheKey.ToString());

    RefPtr<Model> model;

    // Return existing entry without re-importing
    if(m_ModelCache.TryGet(cacheKey, model))
    {
        //TODO(KB) - add a test to confirm cache behavior.

        logDebug("  Model already exists in cache (key: {})", cacheKey.ToString());
        return model;
    }

    std::vector<std::span<const Vertex>> vertexSpans;
    std::vector<std::span<const VertexIndex>> indexSpans;
    for(const auto& meshSpec : modelSpec.MeshSpecs)
    {
        vertexSpans.emplace_back(meshSpec.Vertices);
        indexSpans.emplace_back(meshSpec.Indices);
    }

    auto bufResult = m_GpuDevice->CreateBuffers(vertexSpans, indexSpans);
    expect(bufResult, bufResult.error());

    auto [vb, ib] = bufResult.value();

    std::vector<Mesh> meshes;
    uint32_t indexOffset = 0;

    for (size_t i = 0; i < modelSpec.MeshSpecs.size(); ++i)
    {
        const MeshSpec& meshSpec = modelSpec.MeshSpecs[i];

        auto albedoResult = GetOrCreateTexture(meshSpec.MtlSpec.Albedo);
        expect(albedoResult, albedoResult.error());

        RefPtr<GpuTexture> albedo = albedoResult.value();

        //FIXME - specify number of uniform buffers.
        auto vertexShaderResult = GetOrCreateVertexShader(VertexShaderSpec{meshSpec.MtlSpec.VertexShaderPath, 3});
        expect(vertexShaderResult, vertexShaderResult.error());

        //FIXME - specify number of samplers.
        auto fragShaderResult = GetOrCreateFragmentShader(FragmentShaderSpec{meshSpec.MtlSpec.FragmentShaderPath, 1});
        expect(fragShaderResult, fragShaderResult.error());

        Material mtl
        {
            meshSpec.MtlSpec.Color,
            meshSpec.MtlSpec.Metalness,
            meshSpec.MtlSpec.Roughness,
            albedo,
            vertexShaderResult.value(),
            fragShaderResult.value()
        };

        const uint32_t indexCount = static_cast<uint32_t>(meshSpec.Indices.size());

        auto tmpIb = GpuIndexBuffer(ib.GpuBuffer, ib.Offset + indexOffset * sizeof(VertexIndex));
        Mesh mesh(meshSpec.Name, vb, tmpIb, indexCount, mtl);
        indexOffset += indexCount;

        meshes.emplace_back(mesh);
    }

    std::vector<MeshInstance> meshInstances = modelSpec.MeshInstances;
    std::vector<TransformNode> transformNodes = modelSpec.TransformNodes;

    auto modelResult = Model::Create(std::move(meshes), std::move(meshInstances), std::move(transformNodes));

    expect(modelResult, modelResult.error());

    model = modelResult.value();

    expect(
        m_ModelCache.TryAdd(cacheKey, model),
        "Failed to add model to cache: {}", cacheKey.ToString());

    return model;
}

Result<RefPtr<Model>> ModelCatalog::GetModel(const CacheKey& cacheKey) const
{
    RefPtr<Model> model;
    expect(m_ModelCache.TryGet(cacheKey, model), "Model key not found: {}", cacheKey.ToString());
    return model;
}

// private:

Result<RefPtr<GpuTexture>>
ModelCatalog::GetOrCreateTexture(const TextureSpec& textureSpec)
{
    RefPtr<GpuTexture> texture;
    if(m_TextureCache.TryGet(textureSpec.CacheKey, texture))
    {
        logDebug("  Texture already exists in cache (key: {})", textureSpec.CacheKey.ToString());
        return texture;
    }

    logDebug("  Creating new texture (key: {})", textureSpec.CacheKey.ToString());
    auto result = m_GpuDevice->CreateTexture(textureSpec);
    expect(result, result.error());

    texture = result.value();
    expect(m_TextureCache.TryAdd(textureSpec.CacheKey, texture),
        "Failed to add texture to cache: {}", textureSpec.CacheKey.ToString());

    return texture;
}

Result<RefPtr<GpuVertexShader>>
ModelCatalog::GetOrCreateVertexShader(const VertexShaderSpec& shaderSpec)
{
    // FIXME(KB) - use a proper cache key.
    // Assume the source variant holds a string path for now.
    const CacheKey cacheKey(std::get<0>(shaderSpec.Source));
    
    RefPtr<GpuVertexShader> shader;
    if(m_VertexShaderCache.TryGet(cacheKey, shader))
    {
        logDebug("  Vertex shader already exists in cache (key: {})", cacheKey.ToString());
        return shader;
    }

    auto result = m_GpuDevice->CreateVertexShader(shaderSpec);
    expect(result, result.error());
    shader = result.value();
    expect(m_VertexShaderCache.TryAdd(cacheKey, shader), "Failed to add vertex shader to cache: {}", cacheKey.ToString());
    return shader;
}

Result<RefPtr<GpuFragmentShader>>
ModelCatalog::GetOrCreateFragmentShader(const FragmentShaderSpec& shaderSpec)
{
    // FIXME(KB) - use a proper cache key.
    // Assume the source variant holds a string path for now.
    const CacheKey cacheKey(std::get<0>(shaderSpec.Source));
    
    RefPtr<GpuFragmentShader> shader;
    if(m_FragmentShaderCache.TryGet(cacheKey, shader))
    {
        logDebug("  Fragment shader already exists in cache (key: {})", cacheKey.ToString());
        return shader;
    }

    auto result = m_GpuDevice->CreateFragmentShader(shaderSpec);
    expect(result, result.error());
    shader = result.value();
    expect(m_FragmentShaderCache.TryAdd(cacheKey, shader), "Failed to add fragment shader to cache: {}", cacheKey.ToString());
    return shader;
}

static TextureProperties GetTexturePropertiesFromMaterial(
    const aiMaterial* material,
    const std::filesystem::path& parentPath)
{
    TextureProperties properties;
 
    aiString texPath;
    aiTextureMapping mapping;
    unsigned uvIndex;
    ai_real blend;
    aiTextureOp op;
    aiTextureMapMode mapmode[2];

    if(material->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath, &mapping, &uvIndex, &blend, &op, mapmode) == aiReturn_SUCCESS)
    {
        //DO NOT SUBMIT: Temporary warning for testing
        if(mapmode[0] != aiTextureMapMode_Wrap || mapmode[1] != aiTextureMapMode_Wrap)
        {
            logWarn("Base color texture has non-wrapping UV mode");
        }
        properties.Albedo.Path = (parentPath / texPath.C_Str()).string();
        properties.Albedo.UVIndex = uvIndex;
    }
    if(material->GetTexture(aiTextureType_NORMAL_CAMERA, 0, &texPath, &mapping, &uvIndex, &blend, &op, mapmode) == aiReturn_SUCCESS)
    {
        properties.Normal.Path = (parentPath / texPath.C_Str()).string();
        properties.Normal.UVIndex = uvIndex;
    }
    if(material->GetTexture(aiTextureType_EMISSION_COLOR, 0, &texPath, &mapping, &uvIndex, &blend, &op, mapmode) == aiReturn_SUCCESS)
    {
        properties.Emission.Path = (parentPath / texPath.C_Str()).string();
        properties.Emission.UVIndex = uvIndex;
    }
    if(material->GetTexture(aiTextureType_METALNESS, 0, &texPath, &mapping, &uvIndex, &blend, &op, mapmode) == aiReturn_SUCCESS)
    {
        properties.Metalness.Path = (parentPath / texPath.C_Str()).string();
        properties.Metalness.UVIndex = uvIndex;
    }
    if(material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &texPath, &mapping, &uvIndex, &blend, &op, mapmode) == aiReturn_SUCCESS)
    {
        properties.Roughness.Path = (parentPath / texPath.C_Str()).string();
        properties.Roughness.UVIndex = uvIndex;
    }
    if(material->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &texPath, &mapping, &uvIndex, &blend, &op, mapmode) == aiReturn_SUCCESS)
    {
        properties.AmbientOcclusion.Path = (parentPath / texPath.C_Str()).string();
        properties.AmbientOcclusion.UVIndex = uvIndex;
    }
    return properties;
}

static inline std::string GetMeshName(const aiMesh* mesh)
{
    return mesh->mName.Empty() ? "<unnamed>" : mesh->mName.C_Str();
}

static void LogMesh(const aiScene* scene, const SceneMeshId meshId)
{
    const aiMesh* mesh = scene->mMeshes[meshId];
    logDebug("  Mesh {}: {}", meshId, GetMeshName(mesh));
    logDebug("  Vtx: {}, Tri: {}", mesh->mNumVertices, mesh->mNumFaces);
    const aiMaterial* material = scene->mMaterials ? scene->mMaterials[mesh->mMaterialIndex] : nullptr;
    if(material)
    {
        logDebug("  Material: \"{}\"", material->GetName().C_Str());
    }
};

static void LogMaterialProperties(const aiMaterial* material)
{
    for (unsigned i = 0; i < material->mNumProperties; ++i)
    {
        const aiMaterialProperty* prop = material->mProperties[i];

        if (prop->mKey == aiString("$tex.file"))
        {
            aiString propValue;
            material->Get(prop->mKey.C_Str(), prop->mSemantic, prop->mIndex, propValue);

            logDebug("  Property: key=\"{}\" semantic={} index={} value=\"{}\"",
                prop->mKey.C_Str(),
                prop->mSemantic,
                prop->mIndex,
                propValue.C_Str());
        }
    }
};

static bool ValidateMesh(const aiScene* scene, const unsigned meshIdx)
{
    const aiMesh* mesh = scene->mMeshes[meshIdx];
    if(!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE ))
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
        //TODO - generate normals
        logWarn("Mesh has no normals; skipping");
        LogMesh(scene, meshIdx);
        return false;
    }

    return true;
};

static void CollectMeshes(const aiScene* scene, const aiNode* node, SceneMeshCollection& outCollection)
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

static MaterialSpec CreateMaterialSpec(
    const aiMaterial* material,
    const std::filesystem::path& parentPath)
{
    LogMaterialProperties(material);

    TextureProperties texProperties;
    ai_real opacity{1.0f};
    aiColor3D diffuseColor{1.0f, 1.0f, 1.0f};
    if(material)
    {
        if(aiReturn_SUCCESS != material->Get(AI_MATKEY_OPACITY, &opacity, nullptr))
        {
            opacity = 1.0f;
        }

        if(aiReturn_SUCCESS != material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor))
        {
            diffuseColor = aiColor3D{1.0f, 1.0f, 1.0f};
        }

        logDebug("  Opacity: {}", opacity);
        logDebug("  Diffuse color: R={} G={} B={}", diffuseColor.r, diffuseColor.g, diffuseColor.b);

        texProperties = GetTexturePropertiesFromMaterial(material, parentPath);
    }
    else
    {
        logWarn("  Mesh has no material");
    }

    logDebug("  Albedo: {}", texProperties.Albedo.Path.empty() ? "<none>" : texProperties.Albedo.Path);
    logDebug("  Normal: {}", texProperties.Normal.Path.empty() ? "<none>" : texProperties.Normal.Path);
    logDebug("  Specular: {}", texProperties.Specular.Path.empty() ? "<none>" : texProperties.Specular.Path);
    logDebug("  Diffuse: {}", texProperties.Diffuse.Path.empty() ? "<none>" : texProperties.Diffuse.Path);
    logDebug("  Emission: {}", texProperties.Emission.Path.empty() ? "<none>" : texProperties.Emission.Path);
    logDebug("  Metalness: {}", texProperties.Metalness.Path.empty() ? "<none>" : texProperties.Metalness.Path);
    logDebug("  Roughness: {}", texProperties.Roughness.Path.empty() ? "<none>" : texProperties.Roughness.Path);
    logDebug("  Ambient occlusion: {}", texProperties.AmbientOcclusion.Path.empty() ? "<none>" : texProperties.AmbientOcclusion.Path);

    const TextureSpec albedo = texProperties.Albedo.Path.empty()
     ? MAGENTA_TEXTURE_SPEC
     : TextureSpec{texProperties.Albedo.Path};

    return MaterialSpec
    {
        .Color = RgbaColorf{diffuseColor.r, diffuseColor.g, diffuseColor.b, opacity},
        .Metalness = 0.0f,
        .Roughness = 0.0f,
        .Albedo = albedo,
        .VertexShaderPath = "shaders/Debug/VertexShader",
        .FragmentShaderPath = "shaders/Debug/FragmentShader",
    };
}

static MeshSpec CreateMeshSpecFromMesh(
    const aiScene* scene,
    const SceneMeshId meshId,
    const std::filesystem::path& parentPath)
{
    const aiMesh* mesh = scene->mMeshes[meshId];
    const std::string meshName = GetMeshName(mesh);

    LogMesh(scene, meshId);

    const aiMaterial* material = scene->mMaterials ? scene->mMaterials[mesh->mMaterialIndex] : nullptr;

    const MaterialSpec mtlSpec = CreateMaterialSpec(material, parentPath);

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;
    vertices.reserve(mesh->mNumVertices);
    indices.reserve(mesh->mNumFaces * 3);

    int albedoUvIndex = 0;
    if(material)
    {
        aiGetMaterialInteger(material, AI_MATKEY_UVWSRC(aiTextureType_BASE_COLOR, 0), (int *)&albedoUvIndex);
    }

    // Lambda to get UVs or return zero UVs if not present
    const bool hasAlbedo = mtlSpec.HasAlbedo();
    auto getUV = [hasAlbedo](const aiMesh* mesh, const unsigned uvIndex, unsigned v)
    {
        if (!hasAlbedo)
        {
            return UV2{ 0.0f, 0.0f };
        }

        const aiVector3D& uv = mesh->mTextureCoords[uvIndex][v];
        return UV2{ uv.x, uv.y };
    };
    // Transform mesh by node transform
    for(unsigned v = 0; v < mesh->mNumVertices; ++v)
    {
        const aiVector3D& srcVtx = mesh->mVertices[v];
        const aiVector3D& srcNorm = mesh->mNormals[v];

        Vertex vtx
        {
            .pos = Vec3f{ srcVtx.x, srcVtx.y, srcVtx.z },
            .normal = Vec3f{ srcNorm.x, srcNorm.y, srcNorm.z }.Normalize(),
            .uvs
            {
                getUV(mesh, albedoUvIndex, v)
            }
        };

        vertices.emplace_back(vtx);
    }

    uint32_t indexCount = 0;

    for(unsigned f = 0; f < mesh->mNumFaces; ++f)
    {
        const aiFace& face = mesh->mFaces[f];

        indices.emplace_back(face.mIndices[0]);
        indices.emplace_back(face.mIndices[1]);
        indices.emplace_back(face.mIndices[2]);

        indexCount += 3;
    }

    return MeshSpec
    {
        .Name = meshName,
        .Vertices = std::move(vertices),
        .Indices = std::move(indices),
        .MtlSpec = mtlSpec
    };
}

static MeshSpecCollection CreateMeshSpecCollection(
    const aiScene* scene,
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

static void ProcessNodes(
    const aiNode* node,
    const int parentNodeIndex,
    const MeshSpecCollection& meshSpecCollection,
    std::vector<MeshInstance>& meshInstances,
    std::vector<TransformNode>& transformNodes,
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

        //FIXME(KB) - collapse nodes with no meshes.
        logWarn("  Node {} has no meshes", node->mName.C_Str());
    }

    const aiMatrix4x4& nodeTransform = node->mTransformation;
    const int nodeIndex = static_cast<int>(transformNodes.size());
    
    transformNodes.emplace_back(
        TransformNode
        {
            .ParentIndex = parentNodeIndex,
            .Transform = Mat44f
            {
                // Assimp uses row-major order - transpose to column-major
                nodeTransform.a1, nodeTransform.b1, nodeTransform.c1, nodeTransform.d1,
                nodeTransform.a2, nodeTransform.b2, nodeTransform.c2, nodeTransform.d2,
                nodeTransform.a3, nodeTransform.b3, nodeTransform.c3, nodeTransform.d3,
                nodeTransform.a4, nodeTransform.b4, nodeTransform.c4, nodeTransform.d4,
            }
        });

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
        meshInstances.emplace_back(MeshInstance
            {
                .MeshIndex = meshSpecIndex,
                .NodeIndex = nodeIndex
            });
    }

    for(unsigned i = 0; i < node->mNumChildren; ++i)
    {
        ProcessNodes(
            node->mChildren[i],
            nodeIndex,
            meshSpecCollection,
            meshInstances,
            transformNodes,
            parentPath);
    }
};