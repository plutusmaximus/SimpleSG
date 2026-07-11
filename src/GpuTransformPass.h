#pragma once

#include "shaders/GpuBufferTypes.h"
#include "Result.h"

#include <vector>
#include <webgpu/webgpu_cpp.h>

class FileFetcher;
class GpuHelper;

class GpuTransformPass
{
public:
    static constexpr const char* ShaderPath = "shaders/TransformShader.wgsl";
    static constexpr const char* ComputeEntry = "cs_main";

    struct Resources
    {
        WorldTransformBuffer WorldTransforms;
        ClipSpaceBuffer ClipSpaceTransforms;
        CameraParamsBuffer CameraParams;

        bool Validate() const
        {
            return MLG_VERIFY(WorldTransforms, "Invalid WorldTransforms buffer") &&
                    MLG_VERIFY(ClipSpaceTransforms, "Invalid ClipSpaceTransforms buffer") &&
                    MLG_VERIFY(CameraParams, "Invalid CameraParams buffer");
        }
    };

    ~GpuTransformPass() = default;
    GpuTransformPass(const GpuTransformPass&) = delete;
    GpuTransformPass& operator=(const GpuTransformPass&) = delete;
    GpuTransformPass(GpuTransformPass&&) = default;
    GpuTransformPass& operator=(GpuTransformPass&&) = default;

    static Result<GpuTransformPass> Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> BindResources(const GpuHelper& gpuHelper, const Resources& resources);

    Result<wgpu::ComputePassEncoder> BeginComputePass(const wgpu::CommandEncoder& cmdEncoder) const;

private:
    GpuTransformPass() = default;

    struct PipelineResources
    {
        wgpu::ShaderModule Shader;
        wgpu::BindGroupLayout BindGroupLayout;
        wgpu::PipelineLayout PipelineLayout;
    };

    Result<> EnsurePipeline(const wgpu::Device& gpuDevice);

    std::optional<Resources> m_Resources;

    PipelineResources m_PipelineResources;
    wgpu::BindGroup m_BindGroup;
    wgpu::ComputePipeline m_Pipeline;
    std::vector<uint8_t> m_ShaderCode;
};