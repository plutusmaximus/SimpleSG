#pragma once

struct DrawIndirectBufferParams
{
    uint32_t IndexCount;
    uint32_t InstanceCount;
    uint32_t FirstIndex;
    uint32_t BaseVertex;
    uint32_t FirstInstance;
};

using MaterialIndex = uint32_t;
using TransformIndex = uint32_t;

struct MeshProperties
{
    MaterialIndex MaterialIndex;
};

struct MeshDrawData
{
    TransformIndex TransformIndex;
    MaterialIndex MaterialIndex;
};

struct ModelInstance
{
    uint32_t FirstMesh;
    uint32_t MeshCount;
    TransformIndex TransformIndex;
};

class SceneKit
{
public:

    SceneKit() = default;
    SceneKit(const SceneKit&) = delete;
    SceneKit& operator=(const SceneKit&) = delete;
    SceneKit(SceneKit&&) = delete;
    SceneKit& operator=(SceneKit&&) = delete;

    virtual ~SceneKit() = 0;

    virtual uint32_t GetTransformCount() const = 0;

    virtual uint32_t GetMeshCount() const = 0;
};

inline SceneKit::~SceneKit() = default;