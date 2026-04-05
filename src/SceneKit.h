#pragma once

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