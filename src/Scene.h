#pragma once

#include "PropKit.h"
#include "shaders/ShaderInterop.h"
#include "WebgpuHelper.h"

#include <span>
#include <vector>

// Strongly-typed GPU storage buffer classes.
using WorldTransformBuffer = SemanticGpuBuffer<ShaderInterop::WorldTransform>;
using ClipSpaceBuffer = SemanticGpuBuffer<ShaderInterop::ClipSpaceTransform>;
using MeshPropertiesBuffer = SemanticGpuBuffer<ShaderInterop::MeshProperties>;

struct ModelInstance
{
    ModelIndex ModelIndex{ ModelIndex::INVALID };
};

struct SceneNodeDef
{
    std::string AssemblyName;
    TrsTransformf Transform;
};

struct SceneDef
{
    std::vector<SceneNodeDef> NodeDefs;
};

class PropKit;

class Scene
{
public:
    static Result<> Create(const SceneDef& sceneDef, const PropKit& propKit, Scene& outScene);

    Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&& other) = default;
    Scene& operator=(Scene&& other) = default;

    const std::span<const ModelInstance> GetModelInstances() const { return m_ModelInstances; }

    DrawIndirectBuffer GetDrawIndirectBuffer() const { return m_DrawIndirectBuffer; }

    wgpu::BindGroup GetColorPipelineBindGroup0() const { return m_ColorPipelineBindGroup0; }

    wgpu::BindGroup GetTransformPipelineBindGroup0() const { return m_TransformPipelineBindGroup0; }

private:

    Scene(const PropKit* propKit,
        WorldTransformBuffer transformBuffer,
        DrawIndirectBuffer drawIndirectBuffer,
        MeshPropertiesBuffer meshPropertiesBuffer,
        wgpu::BindGroup colorPipelineBindGroup0,
        wgpu::BindGroup transformPipelineBindGroup0,
        std::vector<ModelInstance>&& modelInstances)
        : m_PropKit(propKit),
          m_TransformBuffer(transformBuffer),
          m_DrawIndirectBuffer(drawIndirectBuffer),
          m_MeshPropertiesBuffer(meshPropertiesBuffer),
          m_ColorPipelineBindGroup0(colorPipelineBindGroup0),
          m_TransformPipelineBindGroup0(transformPipelineBindGroup0),
          m_ModelInstances(std::move(modelInstances))
    {
    }

    const PropKit* m_PropKit;
    WorldTransformBuffer m_TransformBuffer;
    DrawIndirectBuffer m_DrawIndirectBuffer;
    MeshPropertiesBuffer m_MeshPropertiesBuffer;
    wgpu::BindGroup m_ColorPipelineBindGroup0;
    wgpu::BindGroup m_TransformPipelineBindGroup0;
    std::vector<ModelInstance> m_ModelInstances;
};