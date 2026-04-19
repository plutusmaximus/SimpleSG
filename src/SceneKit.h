#pragma once

#include "Bounds.h"
#include "shaders/ShaderTypes.h"
#include "Vertex.h"

#include <string>
#include <vector>

struct MeshProperties
{
    MaterialIndex MaterialIndex;
    AABoundingBox BoundingBox;
};

struct MeshData
{
    uint32_t FirstIndex;
    uint32_t IndexCount;
    uint32_t BaseVertex;
    MeshProperties Properties;
};

struct ModelInstance
{
    uint32_t FirstMesh;
    uint32_t MeshCount;
    TransformIndex TransformIndex;
};

struct MaterialData
{
    std::string BaseTextureUri;
    RgbaColorf Color;
    float Metalness;
    float Roughness;
};

struct TransformData
{
    static constexpr TransformIndex kInvalidParentIndex = std::numeric_limits<TransformIndex>::max();

    Mat44f Transform;
    TransformIndex ParentIndex{ kInvalidParentIndex };
};

class SceneKitSourceData
{
public:

    SceneKitSourceData() = default;
    SceneKitSourceData(const SceneKitSourceData&) = delete;
    SceneKitSourceData& operator=(const SceneKitSourceData&) = delete;
    SceneKitSourceData(SceneKitSourceData&&) = default;
    SceneKitSourceData& operator=(SceneKitSourceData&&) = default;

    std::vector<Vertex> Vertices;
    std::vector<VertexIndex> Indices;
    std::vector<MaterialData> Materials;
    std::vector<TransformData> Transforms;
    std::vector<MeshData> Meshes;
    std::vector<ModelInstance> ModelInstances;
};

class SceneKit
{
public:

    virtual ~SceneKit() = 0;

    virtual uint32_t GetTransformCount() const = 0;

    virtual uint32_t GetMeshCount() const = 0;
};

inline SceneKit::~SceneKit() = default;