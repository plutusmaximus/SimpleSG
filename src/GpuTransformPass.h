#pragma once

#include "GpuTypes.h"
#include "Result.h"

#include <webgpu/webgpu_cpp.h>

class FileFetcher;
class GpuHelper;

class GpuTransformPass
{
public:
    static constexpr const char* ShaderPath = "shaders/TransformShader.wgsl";
    static constexpr const char* ComputeEntry = "cs_main";
    static constexpr const char* kWorkgroupSizeOverride = "WorkgroupSizeOverride";
    static constexpr uint32_t kWorkgroupSize = 64;

    struct Inputs
    {
        GpuWorldTransformBuffer WorldTransforms;
        GpuCameraParamsBuffer CameraParams;

        friend bool operator==(const Inputs& a, const Inputs& b) = default;
    };

    struct Outputs
    {
        GpuClipSpaceBuffer ClipSpaceTransforms;

        friend bool operator==(const Outputs& a, const Outputs& b) = default;
    };

    GpuTransformPass() = delete;
    ~GpuTransformPass() = default;
    GpuTransformPass(const GpuTransformPass&) = delete;
    GpuTransformPass& operator=(const GpuTransformPass&) = delete;
    GpuTransformPass(GpuTransformPass&&) = default;
    GpuTransformPass& operator=(GpuTransformPass&&) = default;

    static Result<GpuTransformPass> Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> SetInputs(const Inputs& inputs);
    Result<> SetOutputs(const Outputs& outputs);

    Result<wgpu::ComputePassEncoder> BeginPass(const wgpu::CommandEncoder& cmdEncoder);

private:
    explicit GpuTransformPass(const GpuHelper& gpuHelper,
        wgpu::ShaderModule shader,
        wgpu::BindGroupLayout bindGroupLayout,
        wgpu::PipelineLayout pipelineLayout)
        : m_GpuHelper(&gpuHelper),
          m_Shader(std::move(shader)),
          m_BindGroupLayout(std::move(bindGroupLayout)),
          m_PipelineLayout(std::move(pipelineLayout))
    {
        MLG_ASSERT(m_Shader, "Shader module is not valid");
        MLG_ASSERT(m_BindGroupLayout, "Bind group layout is not valid");
        MLG_ASSERT(m_PipelineLayout, "Pipeline layout is not valid");
    }

    Result<> EnsurePipeline();
    Result<> EnsureInputOutputBindGroup();

    const GpuHelper* m_GpuHelper;

    std::optional<Inputs> m_Inputs;
    std::optional<Outputs> m_Outputs;

    wgpu::ShaderModule m_Shader;
    wgpu::BindGroupLayout m_BindGroupLayout;
    wgpu::PipelineLayout m_PipelineLayout;
    wgpu::BindGroup m_InputOutputBindGroup;
    wgpu::ComputePipeline m_Pipeline;
};