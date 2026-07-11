#pragma once

#include "Result.h"

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

    struct Inputs
    {
        wgpu::Texture Texture;

        Result<> Validate() const
        {
            MLG_CHECKV(Texture, "Input texture is not valid");

            return Result<>::Ok;
        }

        friend bool operator==(const Inputs& lhs, const Inputs& rhs)
        {
            return lhs.Texture.Get() == rhs.Texture.Get();
        }
    };

    struct Outputs
    {
        wgpu::Texture Texture;

        Result<> Validate() const
        {
            MLG_CHECKV(Texture, "Output texture is not valid");

            return Result<>::Ok;
        }

        friend bool operator==(const Outputs& lhs, const Outputs& rhs)
        {
            return lhs.Texture.Get() == rhs.Texture.Get();
        }
    };

    ~GpuCompositorPass() = default;
    GpuCompositorPass(const GpuCompositorPass&) = delete;
    GpuCompositorPass& operator=(const GpuCompositorPass&) = delete;
    GpuCompositorPass(GpuCompositorPass&&) = default;
    GpuCompositorPass& operator=(GpuCompositorPass&&) = default;

    static Result<GpuCompositorPass> Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> BindInputs(const GpuHelper& gpuHelper, const Inputs& inputs);
    Result<> BindOutputs(const GpuHelper& gpuHelper, const Outputs& outputs);

    Result<wgpu::RenderPassEncoder> BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder) const;

    Result<> Composite(const GpuHelper& gpuHelper, const wgpu::Texture& target) const;

    Result<> Composite(
        const GpuHelper& gpuHelper, const wgpu::Texture& target, const Rect& dstRect) const;

private:
    GpuCompositorPass() = default;

    Result<> EnsureSampler(const wgpu::Device& gpuDevice);
    Result<> EnsureBindGroupLayout(const wgpu::Device& gpuDevice);
    Result<> EnsurePipeline(const wgpu::Device& gpuDevice, wgpu::TextureFormat targetFormat);

    Inputs m_Inputs;
    Outputs m_Outputs;

    wgpu::ShaderModule m_Shader;
    wgpu::BindGroupLayout m_BindGroupLayout;
    wgpu::PipelineLayout m_PipelineLayout;
    wgpu::Sampler m_Sampler;
    wgpu::TextureFormat m_TargetFormat{ wgpu::TextureFormat::Undefined };
    wgpu::BindGroup m_BindGroup;
    wgpu::RenderPipeline m_Pipeline;
};