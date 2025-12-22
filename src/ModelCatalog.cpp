#include "ModelCatalog.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

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
        out.uvs[0] = UV2{ uv.x, uv.y };
    }
    else
    {
        out.uvs[0] = UV2{ 0, 0 };
    }

    return out;
}

Result<ModelSpec> ModelCatalog::LoadFromFile(const std::string& key, const std::string& filePath)
{
    // Return existing entry without re-importing
    auto it = m_Entries.find(key);
    if(it != m_Entries.end())
    {
        return it->second.ToSpec();
    }

    Entry entry;

    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        filePath,
        aiProcess_Triangulate            |
        aiProcess_JoinIdenticalVertices  |
        aiProcess_FlipWindingOrder       | // glTF uses CCW; engine uses CW
        aiProcess_MakeLeftHanded         |
        aiProcess_PreTransformVertices   | // Apply node transforms to vertices
        aiProcess_SortByPType            |
        aiProcess_GenSmoothNormals);

    expect(scene != nullptr, importer.GetErrorString());
    expect(scene->mNumMeshes > 0, "No meshes in model: {}", filePath);

    // Flatten all meshes into contiguous buffers
    for(unsigned meshIdx = 0; meshIdx < scene->mNumMeshes; ++meshIdx)
    {
        const aiMesh* mesh = scene->mMeshes[meshIdx];
        eassert(mesh->mPrimitiveTypes & aiPrimitiveType_TRIANGLE, "Only triangle meshes are supported");
        expect(mesh->mNumVertices > 0 && mesh->mNumFaces > 0, "Empty mesh {}", meshIdx);

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
                RgbaColorf{1, 1, 1},
                "shaders/Debug/VertexShader",
                "shaders/Debug/FragmentShader",
                0.0f,
                0.0f,
                "images/Ant.png"
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
