#pragma once

#include "Bounds.h"
#include "Result.h"
#include "TextureCache.h"
#include "VecMath.h"
#include "ShaderInterop.h"

#include <filesystem>
#include <span>
#include <vector>
#include <unordered_map>

struct MaterialIndexTag {};
struct MeshIndexTag {};
struct ModelIndexTag {};
struct NodeIndexTag {};
using MaterialIndex = SemanticInteger<MaterialIndexTag>;
using MeshIndex = SemanticInteger<MeshIndexTag>;
using ModelIndex = SemanticInteger<ModelIndexTag>;
using NodeIndex = SemanticInteger<NodeIndexTag>;

struct Mesh
{
    uint32_t IndexCount;
    uint32_t FirstIndex;
    uint32_t BaseVertex;
    MaterialIndex MaterialIndex;
    AABoundingBox BoundingBox;
};

struct Model
{
    std::string Name;
    MeshIndex FirstMesh{ MeshIndex::INVALID };
    uint32_t MeshCount{ 0 };
};

struct AssemblyNode
{
    Mat44f Transform;
    ModelIndex ModelIndex{ ModelIndex::INVALID };
    NodeIndex ParentIndex{ NodeIndex::INVALID };
    uint32_t ChildCount{ 0 };
};

struct MaterialDef
{
    std::string BaseTextureUri;
    RgbaColorf Color;
    float Metalness;
    float Roughness;
};

struct MeshDef
{
    std::vector<Vertex> Vertices;
    std::vector<VertexIndex> Indices;
    MaterialDef MaterialDef;
};

struct ModelDef
{
    std::string Name;
    std::vector<MeshDef> MeshDefs;
};

struct AssemblyNodeDef
{
    std::string Name;
    Mat44f Transform;
    ModelIndex ModelIndex{ ModelIndex::INVALID };
    std::vector<AssemblyNodeDef> Children;
};

struct PropKitDef
{
    std::vector<ModelDef> ModelDefs;
    std::vector<AssemblyNodeDef> AssemblyDefs;
};

// Strongly-typed GPU storage buffer classes.
using MeshPropertiesBuffer = SemanticGpuBuffer<ShaderInterop::MeshProperties>;
using MaterialConstantsBuffer = SemanticGpuBuffer<ShaderInterop::MaterialConstants>;

class PropKit
{
public:
    static Result<> Create(const std::filesystem::path& rootPath,
        TextureCache& textureCache,
        const PropKitDef& propKitDef,
        PropKit& outPropKit);

    PropKit() = default;
    PropKit(const PropKit&) = delete;
    PropKit& operator=(const PropKit&) = delete;
    PropKit(PropKit&& other) = default;
    PropKit& operator=(PropKit&& other) = default;

    uint32_t GetMeshCount() const { return static_cast<uint32_t>(m_Meshes.size()); }

    const std::span<const wgpu::BindGroup> GetMaterialBindGroups() const
    {
        return m_MaterialBindGroups;
    }

    MaterialConstantsBuffer GetMaterialConstantsBuffer() const { return m_MaterialConstantsBuffer; }

    const std::span<const Mesh> GetMeshes() const { return m_Meshes; }

    const std::span<const Model> GetModels() const { return m_Models; }

    const std::span<const AssemblyNode> GetAssemblies() const { return m_AssemblyNodes; }

    VertexBuffer GetVertexBuffer() const { return m_VertexBuffer; }

    IndexBuffer GetIndexBuffer() const { return m_IndexBuffer; }

    Result<const AssemblyNode*> GetAssembly(const std::string& name) const
    {
        auto it = m_AssemblyNameToIndex.find(name);
        MLG_CHECK(it != m_AssemblyNameToIndex.end(), "Assembly not found: {}", name);

        return &m_AssemblyNodes[it->second.Value()];
    }

private:
    PropKit(VertexBuffer vertexBuffer,
        IndexBuffer indexBuffer,
        std::vector<Mesh>&& meshes,
        std::vector<Model>&& models,
        std::vector<AssemblyNode>&& assemblyNodes,
        std::unordered_map<std::string, NodeIndex>&& assemblyNameToIndex,
        MaterialConstantsBuffer materialConstantsBuffer,
        std::vector<wgpu::BindGroup>&& materialBindGroups);

    VertexBuffer m_VertexBuffer;
    IndexBuffer m_IndexBuffer;
    std::vector<Mesh> m_Meshes;
    std::vector<Model> m_Models;
    std::vector<AssemblyNode> m_AssemblyNodes;
    MaterialConstantsBuffer m_MaterialConstantsBuffer;
    std::vector<wgpu::BindGroup> m_MaterialBindGroups;
    std::unordered_map<std::string, ModelIndex> m_ModelNameToIndex;
    std::unordered_map<std::string, NodeIndex> m_AssemblyNameToIndex;
};