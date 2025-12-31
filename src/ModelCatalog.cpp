#include "ModelCatalog.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include <filesystem>

struct TexturePaths
{
    std::string Albedo;
    std::string Diffuse;
    std::string Specular;
    std::string Normal;
    std::string Emission;
    std::string Metalness;
    std::string Roughness;
    std::string AmbientOcclusion;
};

static TexturePaths GetTexturePathsFromMaterial(
    const aiMaterial* material,
    const std::filesystem::path& parentPath);

static inline Vertex AiVertexToVertex(const aiMesh* mesh, const unsigned i)
{
    const aiVector3D& v = mesh->mVertices[i];
    const aiVector3D& n = mesh->mNormals[i];

    Vertex out{};
    out.pos = Vec3f{ v.x, v.y, v.z };
    out.normal = Vec3f{ n.x, n.y, n.z };

    if(mesh->mTextureCoords[0])
    {
        const aiVector3D& uv = mesh->mTextureCoords[0][i];
        //out.uvs[0] = UV2{ uv.x - std::floor(uv.x), uv.y - std::floor(uv.y) };
        out.uvs[0] = UV2{ uv.x, uv.y };

        //logDebug("Vertex {}: XYZ=({}, {}, {}) UV = ({}, {})", i, v.x, v.y, v.z, uv.x, uv.y);
    }
    else
    {
        out.uvs[0] = UV2{ 0, 0 };
    }

    return out;
}

Result<ModelSpec> ModelCatalog::LoadFromFile(const std::string& key, const std::string& filePath)
{
    logDebug("Loading model from file: {} (key: {})", filePath, key);

    std::filesystem::path absPath = std::filesystem::absolute(filePath);
    
    const auto parentPath = absPath.parent_path();

    // Return existing entry without re-importing
    auto it = m_Entries.find(key);
    if(it != m_Entries.end())
    {
        return it->second.ToSpec();
    }

    Entry entry;

    constexpr unsigned flags =
        (
        aiProcessPreset_TargetRealtime_Quality
        | aiProcess_FlipWindingOrder        // glTF uses CCW; engine uses CW
        | aiProcess_MakeLeftHanded          // Convert to left-handed coords
        // Assimp's UV coordinate system has V=0 at bottom.
        // Our convention is V=0 at top.
        | aiProcess_FlipUVs                 // Flip UV coordinates
        | aiProcess_PreTransformVertices    // Apply node transforms to vertices
        //| aiProcess_DropNormals          // We generate our own normals
        //| aiProcess_ForceGenNormals     // Ensure normals exist
        ) & ~
        (
            aiProcess_SplitLargeMeshes
        )
    ;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(filePath, flags);

    expect(scene != nullptr, importer.GetErrorString());
    expect(scene->mNumMeshes > 0, "No meshes in model: {}", filePath);

    // Flatten all meshes into contiguous buffers
    for(unsigned meshIdx = 0; meshIdx < scene->mNumMeshes; ++meshIdx)
    {
        const aiMesh* mesh = scene->mMeshes[meshIdx];

        logDebug("Processing mesh \"{}\"({}): {} vertices, {} faces",
            mesh->mName.Empty() ? "<unnamed>" : mesh->mName.C_Str(),
            meshIdx, mesh->mNumVertices, mesh->mNumFaces);

        if(!(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE ))
        {
            logWarn("Skipping non-triangle mesh {}", meshIdx);
            continue;
        }

        if(mesh->mNumVertices == 0 || mesh->mNumFaces == 0)
        {
            logWarn("Skipping empty mesh {}", meshIdx);
            continue;
        }

        if(!mesh->HasNormals())
        {
            //TODO - generate normals
            logWarn("Mesh {} has no normals", meshIdx);
            continue;
        }

        // Only support first UV set for now
        for(int i = 1; i < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++i)
        {
            if(mesh->mTextureCoords[i])
            {
                logWarn("Mesh {} has multiple texture coordinate sets; only using first", meshIdx);
                break;
            }
        }

        std::string albedo;

        const aiMaterial* material = scene->mMaterials ? scene->mMaterials[mesh->mMaterialIndex] : nullptr;

        TexturePaths texPaths;
        ai_real opacity{1.0f};
        aiColor3D diffuseColor{1.0f, 1.0f, 1.0f};
        if(material)
        {
            logDebug("Processing material \"{}\"",
                material->GetName().C_Str());

            for(int propIdx = 0;
                material && propIdx < material->mNumProperties;
                ++propIdx)
            {
                const aiMaterialProperty* prop = material->mProperties[propIdx];
                logDebug("  Property {}: key=\"{}\", semantic={}, index={}, type={}, dataLength={}",
                    propIdx,
                    prop->mKey.C_Str(),
                    prop->mSemantic,
                    prop->mIndex,
                    static_cast<int>(prop->mType),
                    prop->mDataLength);
            }

            if(aiReturn_SUCCESS != material->Get(AI_MATKEY_OPACITY, &opacity, nullptr))
            {
                opacity = 1.0f;
            }

            if(aiReturn_SUCCESS != material->Get(AI_MATKEY_COLOR_DIFFUSE, diffuseColor))
            {
                diffuseColor = aiColor3D{1.0f, 1.0f, 1.0f};
            }

            texPaths = GetTexturePathsFromMaterial(material, parentPath);
        }
        else
        {
            logWarn("Mesh {} has no material", meshIdx);
        }

        if(!texPaths.Albedo.empty())
        {
            logDebug("Mesh {} has base color texture: {}", meshIdx, texPaths.Albedo);
            albedo = texPaths.Albedo;
        }
        else
        {
            logWarn("Mesh {} has no base color texture", meshIdx);
            albedo = "images/Ant.png"; // Default texture
        }

        const uint32_t baseVertex = static_cast<uint32_t>(entry.Vertices.size());
        const uint32_t baseIndexOffset = static_cast<uint32_t>(entry.Indices.size());

        entry.Vertices.reserve(entry.Vertices.size() + mesh->mNumVertices);
        for(unsigned i = 0; i < mesh->mNumVertices; ++i)
        {
            entry.Vertices.emplace_back(AiVertexToVertex(mesh, i));
        }

        entry.Indices.reserve(entry.Indices.size() + mesh->mNumFaces * 3);
        for(unsigned f = 0; f < mesh->mNumFaces; ++f)
        {
            const aiFace& face = mesh->mFaces[f];
            expect(face.mNumIndices == 3, "Non-triangle face encountered");
            for(unsigned j = 0; j < face.mNumIndices; ++j)
            {
                entry.Indices.emplace_back(baseVertex + face.mIndices[j]);
            }
        }

        // Basic material spec defaults; shaders can be swapped by callers later
        MeshSpec spec{
            baseIndexOffset,
            static_cast<uint32_t>(entry.Indices.size()) - baseIndexOffset,
            MaterialSpec{
                RgbaColorf{diffuseColor.r, diffuseColor.g, diffuseColor.b, opacity},
                "shaders/Debug/VertexShader",
                "shaders/Debug/FragmentShader",
                0.0f,
                0.0f,
                albedo
            }
        };

        entry.MeshSpecs.emplace_back(spec);
    }

    // Insert and return spec
    auto [insertIt, inserted] = m_Entries.emplace(key, std::move(entry));
    expect(inserted, "Failed to insert catalog entry for {}", key);

    return insertIt->second.ToSpec();
}

Result<ModelSpec> ModelCatalog::Get(const std::string& key) const
{
    auto it = m_Entries.find(key);
    expect(it != m_Entries.end(), "Model key not found: {}", key);
    return it->second.ToSpec();
}

static TexturePaths GetTexturePathsFromMaterial(
    const aiMaterial* material,
    const std::filesystem::path& parentPath)
{
    TexturePaths outPaths;
 
    aiString texPath;
    aiTextureMapping mapping;
    unsigned int uvIndex;
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
        outPaths.Albedo = (parentPath / texPath.C_Str()).string();
    }
    if(material->GetTexture(aiTextureType_NORMAL_CAMERA, 0, &texPath, &mapping, &uvIndex, &blend, &op, mapmode) == aiReturn_SUCCESS)
    {
        outPaths.Normal = (parentPath / texPath.C_Str()).string();
    }
    if(material->GetTexture(aiTextureType_EMISSION_COLOR, 0, &texPath, &mapping, &uvIndex, &blend, &op, mapmode) == aiReturn_SUCCESS)
    {
        outPaths.Emission = (parentPath / texPath.C_Str()).string();
    }
    if(material->GetTexture(aiTextureType_METALNESS, 0, &texPath, &mapping, &uvIndex, &blend, &op, mapmode) == aiReturn_SUCCESS)
    {
        outPaths.Metalness = (parentPath / texPath.C_Str()).string();
    }
    if(material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &texPath, &mapping, &uvIndex, &blend, &op, mapmode) == aiReturn_SUCCESS)
    {
        outPaths.Roughness = (parentPath / texPath.C_Str()).string();
    }
    if(material->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &texPath, &mapping, &uvIndex, &blend, &op, mapmode) == aiReturn_SUCCESS)
    {
        outPaths.AmbientOcclusion = (parentPath / texPath.C_Str()).string();
    }
    return outPaths;
}