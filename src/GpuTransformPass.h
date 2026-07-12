#pragma once

#include "shaders/GpuBufferTypes.h"
#include "Result.h"

#include <webgpu/webgpu_cpp.h>

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

        bool Validate() const
        {
            return MLG_VERIFY(WorldTransforms, "Invalid WorldTransforms buffer") &&
                    MLG_VERIFY(CameraParams, "Invalid CameraParams buffer");
        }
    };

    struct Outputs
    {
        ClipSpaceBuffer ClipSpaceTransforms;

        bool Validate() const
        {
            return MLG_VERIFY(ClipSpaceTransforms, "Invalid ClipSpaceTransforms buffer");
        }
    };

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
    GpuTransformPass() = default;

    Result<> EnsureBindgroup(const wgpu::Device& gpuDevice);
    Result<> EnsurePipeline(const wgpu::Device& gpuDevice);

    std::optional<Inputs> m_Inputs;
    std::optional<Outputs> m_Outputs;

    wgpu::ShaderModule m_Shader;
    wgpu::BindGroupLayout m_BindGroupLayout;
    wgpu::PipelineLayout m_PipelineLayout;
    wgpu::BindGroup m_BindGroup;
    wgpu::ComputePipeline m_Pipeline;
};