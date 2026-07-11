#pragma once

#include "Result.h"

#include <cstdint>
#include <vector>
#include <webgpu/webgpu_cpp.h>

class FileFetcher;
class GpuHelper;
class Rect;

class GpuCompositorPass
{
public:
    static constexpr const char* ShaderPath = "shaders/CompositorShader.wgsl";
    static constexpr const char* VertexEntry = "vs_main";
    static constexpr const char* FragmentEntry = "fs_main";

    struct Resources
    {
        wgpu::Texture SourceTexture;
        wgpu::Texture TargetTexture;

        Result<> Validate() const
        {
            MLG_CHECKV(SourceTexture, "Source texture is not valid");
            MLG_CHECKV(TargetTexture, "Target texture is not valid");

            return Result<>::Ok;
        }
    };

    ~GpuCompositorPass() = default;
    GpuCompositorPass(const GpuCompositorPass&) = delete;
    GpuCompositorPass& operator=(const GpuCompositorPass&) = delete;
    GpuCompositorPass(GpuCompositorPass&&) = default;
    GpuCompositorPass& operator=(GpuCompositorPass&&) = default;

    static Result<GpuCompositorPass> Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> BindResources(const GpuHelper& gpuHelper, const Resources& resources);

    Result<wgpu::RenderPassEncoder> BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder) const;

    Result<> Composite(const GpuHelper& gpuHelper, const wgpu::Texture& target) const;

    Result<> Composite(
        const GpuHelper& gpuHelper, const wgpu::Texture& target, const Rect& dstRect) const;

private:
    GpuCompositorPass() = default;

    struct PipelineResources
    {
        wgpu::ShaderModule Shader;
        wgpu::BindGroupLayout BindGroupLayout;
        wgpu::PipelineLayout Layout;
        wgpu::Sampler Sampler;
        wgpu::TextureFormat TargetFormat{ wgpu::TextureFormat::Undefined };
    };

    Result<> EnsurePipeline(const wgpu::Device& gpuDevice, wgpu::TextureFormat targetFormat);

    Resources m_Resources;

    PipelineResources m_PipelineResources;
    wgpu::BindGroup m_BindGroup;
    wgpu::RenderPipeline m_Pipeline;
    std::vector<uint8_t> m_ShaderCode;
};