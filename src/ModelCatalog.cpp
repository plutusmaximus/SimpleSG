#include "ModelCatalog.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <filesystem>

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

/// @brief Collection of meshes in a scene.
struct MeshCollection
{
    std::unordered_map<unsigned, const aiMesh*> Meshes;
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
static void CollectMeshes(const aiScene* scene, const aiNode* node, MeshCollection& outCollection);

/// @brief Processes meshes in a scene node and its children, applying transformations and collecting vertex/index data.
static void ProcessMeshes(
        const aiScene* scene,
        const aiNode* node,
        const aiMatrix4x4& parentTransform,
        const MeshCollection& meshCollection,
        std::vector<MeshSpec>& meshSpecs,
        std::vector<MeshInstance>& meshInstances,
        const std::filesystem::path& parentPath);

Result<const ModelSpec*>
ModelCatalog::LoadFromFile(const std::string& key, const std::string& filePath)
{
    logDebug("Loading model from file: {} (key: {})", filePath, key);

    // Return existing entry without re-importing
    auto it = m_Entries.find(key);
    if(it != m_Entries.end())
    {
        return &it->second;
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

    MeshCollection meshCollection;
    CollectMeshes(scene, scene->mRootNode, meshCollection);

    std::vector<MeshSpec> meshSpecs;
    std::vector<MeshInstance> meshInstances;

    meshSpecs.reserve(meshCollection.Meshes.size());

    const auto absPath = std::filesystem::absolute(filePath);    
    const auto parentPath = absPath.parent_path();

    ProcessMeshes(
        scene,
        scene->mRootNode,
        aiMatrix4x4(),
        meshCollection,
        meshSpecs,
        meshInstances,
        parentPath);

    // Insert and return spec
    auto [insertIt, inserted] = m_Entries.emplace(key, ModelSpec{std::move(meshSpecs), std::move(meshInstances)});
    expect(inserted, "Failed to insert catalog entry for {}", key);

    return &insertIt->second;
}

Result<const ModelSpec*> ModelCatalog::Get(const std::string& key) const
{
    auto it = m_Entries.find(key);
    expect(it != m_Entries.end(), "Model key not found: {}", key);
    return &it->second;
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

static void LogMesh(const aiScene* scene, const unsigned meshIdx)
{
    const aiMesh* mesh = scene->mMeshes[meshIdx];
    logDebug("  Mesh {}: {}", meshIdx, GetMeshName(mesh));
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

static void CollectMeshes(const aiScene* scene, const aiNode* node, MeshCollection& outCollection)
{
    for(unsigned i = 0; i < node->mNumMeshes; ++i)
    {
        const unsigned meshIdx = node->mMeshes[i];
        if(!ValidateMesh(scene, meshIdx))
        {
            continue;
        }

        const aiMesh* mesh = scene->mMeshes[meshIdx];

        outCollection.Meshes[meshIdx] = mesh;
    }

    for(unsigned i = 0; i < node->mNumChildren; ++i)
    {
        CollectMeshes(scene, node->mChildren[i], outCollection);
    }
};

static void ProcessMeshes(
        const aiScene* scene,
        const aiNode* node,
        const aiMatrix4x4& parentTransform,
        const MeshCollection& meshCollection,
        std::vector<MeshSpec>& meshSpecs,
        std::vector<MeshInstance>& meshInstances,
        const std::filesystem::path& parentPath)
{
    const aiMatrix4x4 globalTransform = parentTransform * node->mTransformation;
    const aiMatrix3x3 normalMatrix = aiMatrix3x3(globalTransform);

    for(unsigned i = 0; i < node->mNumMeshes; ++i)
    {
        const unsigned meshIdx = node->mMeshes[i];
        if(!meshCollection.Meshes.contains(meshIdx))
        {
            continue;
        }

        const aiMesh* mesh = meshCollection.Meshes.at(meshIdx);

        const std::string meshName = GetMeshName(mesh);

        logDebug("Processing mesh");
        LogMesh(scene, meshIdx);

        const aiMaterial* material = scene->mMaterials ? scene->mMaterials[mesh->mMaterialIndex] : nullptr;

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

        auto albedo = !texProperties.Albedo.Path.empty() ? texProperties.Albedo.Path : GPUDevice::MAGENTA_TEXTURE_KEY;

        std::vector<Vertex> vertices;
        std::vector<VertexIndex> indices;
        vertices.reserve(mesh->mNumVertices);
        indices.reserve(mesh->mNumFaces * 3);

        // Lambda to get UVs or return zero UVs if not present
        auto getUV = texProperties.Albedo.Path.empty()
            ? [](const aiMesh* mesh, const unsigned uvIndex, unsigned v) { return UV2{0.0f, 0.0f}; }
            : [](const aiMesh* mesh, const unsigned uvIndex, unsigned v)
            {
                const aiVector3D& uv = mesh->mTextureCoords[uvIndex][v];
                return UV2{ uv.x, uv.y };
            };

        // Transform mesh by node transform
        for(unsigned v = 0; v < mesh->mNumVertices; ++v)
        {
            const aiVector3D transformedVert = globalTransform * mesh->mVertices[v];
            const aiVector3D transformedNorm = normalMatrix * mesh->mNormals[v];

            Vertex vtx
            {
                .pos = Vec3f{ transformedVert.x, transformedVert.y, transformedVert.z },
                .normal = Vec3f{ transformedNorm.x, transformedNorm.y, transformedNorm.z }.Normalize(),
                .uvs
                {
                    getUV(mesh, texProperties.Albedo.UVIndex, v)
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

        MeshSpec spec
        {
            .Name = meshName,
            .Vertices = std::move(vertices),
            .Indices = std::move(indices),
            .MtlSpec = MaterialSpec{
                .Color = RgbaColorf{diffuseColor.r, diffuseColor.g, diffuseColor.b, opacity},
                .VertexShader = "shaders/Debug/VertexShader",
                .FragmentShader = "shaders/Debug/FragmentShader",
                .Metalness = 0.0f,
                .Roughness = 0.0f,
                .Albedo = albedo
            }
        };

        meshSpecs.emplace_back(std::move(spec));
        meshInstances.emplace_back(MeshInstance{ static_cast<int>(meshSpecs.size() - 1), -1 });
    }

    for(unsigned i = 0; i < node->mNumChildren; ++i)
    {
        ProcessMeshes(
            scene,
            node->mChildren[i],
            globalTransform,
            meshCollection,
            meshSpecs,
            meshInstances,
            parentPath);
    }
};