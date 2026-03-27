#pragma once

class ScenePack
{
public:

    ScenePack() = default;
    ScenePack(const ScenePack&) = delete;
    ScenePack& operator=(const ScenePack&) = delete;
    ScenePack(ScenePack&&) = delete;
    ScenePack& operator=(ScenePack&&) = delete;

    virtual ~ScenePack() = 0;

    virtual uint32_t GetTransformCount() const = 0;

    virtual uint32_t GetMeshCount() const = 0;
};

inline ScenePack::~ScenePack() = default;