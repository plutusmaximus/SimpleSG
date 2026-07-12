#pragma once

#include "GpuTypes.h"
#include "Result.h"

class FileFetcher;
class GpuHelper;

class GpuTransformPass
{
public:
    static constexpr const char* ShaderPath = "shaders/TransformShader.wgsl";
    static constexpr const char* ComputeEntry = "cs_main";

    struct Inputs
    {
        WorldTransformBuffer WorldTransforms;
        CameraParamsBuffer CameraParams;

        friend bool operator==(const Inputs& a, const Inputs& b)
        {
            return a.WorldTransforms == b.WorldTransforms && a.CameraParams == b.CameraParams;
        }
    };

    struct Outputs
    {
        ClipSpaceBuffer ClipSpaceTransforms;

        friend bool operator==(const Outputs& a, const Outputs& b)
        {
            return a.ClipSpaceTransforms == b.ClipSpaceTransforms;
        }
    };

    GpuTransformPass() = delete;
    ~GpuTransformPass() = default;
    GpuTransformPass(const GpuTransformPass&) = delete;
    GpuTransformPass& operator=(const GpuTransformPass&) = delete;
    GpuTransformPass(GpuTransformPass&&) = default;
    GpuTransformPass& operator=(GpuTransformPass&&) = default;

    static Result<GpuTransformPass> Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> SetInputs(const GpuHelper& gpuHelper, const Inputs& inputs);
    Result<> SetOutputs(const GpuHelper& gpuHelper, const Outputs& outputs);

    Result<wgpu::ComputePassEncoder> BeginPass(const wgpu::CommandEncoder& cmdEncoder) const;

private:

    explicit GpuTransformPass(ValidShaderModule shader)
        : m_Shader(std::move(shader))
    {
    }

    Result<> EnsureBindgroup(const wgpu::Device& gpuDevice);
    Result<> EnsurePipeline(const wgpu::Device& gpuDevice);

    std::optional<Inputs> m_Inputs;
    std::optional<Outputs> m_Outputs;

    ValidShaderModule m_Shader;
    wgpu::BindGroupLayout m_BindGroupLayout;
    wgpu::PipelineLayout m_PipelineLayout;
    wgpu::BindGroup m_BindGroup;
    wgpu::ComputePipeline m_Pipeline;
};