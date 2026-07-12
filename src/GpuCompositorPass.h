#pragma once

#include "Result.h"
#include "VecMath.h"

#include <webgpu/webgpu_cpp.h>

class FileFetcher;
class GpuHelper;

class GpuCompositorPass
{
public:
    static constexpr const char* ShaderPath = "shaders/CompositorShader.wgsl";
    static constexpr const char* VertexEntry = "vs_main";
    static constexpr const char* FragmentEntry = "fs_main";

    struct Inputs
    {
        Rect DstRect = Rect({.X = -1, .Y = -1, .Width = 1, .Height = 1});
        wgpu::Texture Texture;

        Result<> Validate() const
        {
            MLG_CHECKV(Texture, "Input texture is not valid");

            return Result<>::Ok;
        }

        friend bool operator==(const Inputs& a, const Inputs& b)
        {
            return a.Texture.Get() == b.Texture.Get() && a.DstRect == b.DstRect;
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

        friend bool operator==(const Outputs& a, const Outputs& b)
        {
            return a.Texture.Get() == b.Texture.Get();
        }
    };

    ~GpuCompositorPass() = default;
    GpuCompositorPass(const GpuCompositorPass&) = delete;
    GpuCompositorPass& operator=(const GpuCompositorPass&) = delete;
    GpuCompositorPass(GpuCompositorPass&&) = default;
    GpuCompositorPass& operator=(GpuCompositorPass&&) = default;

    static Result<GpuCompositorPass> Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> SetInputs(const GpuHelper& gpuHelper, const Inputs& inputs);
    Result<> SetOutputs(const GpuHelper& gpuHelper, const Outputs& outputs);

    Result<wgpu::RenderPassEncoder> BeginPass(const wgpu::CommandEncoder& cmdEncoder) const;

    Result<> Composite(const GpuHelper& gpuHelper) const;

private:
    GpuCompositorPass() = default;

    Result<> EnsureSampler(const wgpu::Device& gpuDevice);
    Result<> EnsureBindGroupLayout(const wgpu::Device& gpuDevice);
    Result<> EnsurePipeline(const wgpu::Device& gpuDevice);

    Inputs m_Inputs;
    Outputs m_Outputs;

    wgpu::ShaderModule m_Shader;
    wgpu::BindGroupLayout m_BindGroupLayout;
    wgpu::PipelineLayout m_PipelineLayout;
    wgpu::Sampler m_Sampler;
    wgpu::BindGroup m_BindGroup;
    wgpu::RenderPipeline m_Pipeline;
};