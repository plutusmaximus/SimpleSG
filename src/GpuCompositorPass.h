#pragma once

#include "GpuTypes.h"
#include "VecMath.h"

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
        GpuValidTexture Texture;

        friend bool operator==(const Inputs& a, const Inputs& b) = default;
    };

    struct Outputs
    {
        GpuValidTexture Texture;

        friend bool operator==(const Outputs& a, const Outputs& b) = default;
    };

    GpuCompositorPass() = delete;
    ~GpuCompositorPass() = default;
    GpuCompositorPass(const GpuCompositorPass&) = delete;
    GpuCompositorPass& operator=(const GpuCompositorPass&) = delete;
    GpuCompositorPass(GpuCompositorPass&&) = default;
    GpuCompositorPass& operator=(GpuCompositorPass&&) = default;

    static Result<GpuCompositorPass> Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> SetInputs(const Inputs& inputs);
    Result<> SetOutputs(const Outputs& outputs);

    Result<wgpu::RenderPassEncoder> BeginPass(const wgpu::CommandEncoder& cmdEncoder);

    Result<> Composite();

private:
    explicit GpuCompositorPass(const GpuHelper& gpuHelper,
        wgpu::ShaderModule shader,
        wgpu::Sampler sampler,
        wgpu::BindGroupLayout bindGroupLayout,
        wgpu::PipelineLayout pipelineLayout)
        : m_GpuHelper(&gpuHelper),
          m_Shader(std::move(shader)),
          m_Sampler(std::move(sampler)),
          m_BindGroupLayout(std::move(bindGroupLayout)),
          m_PipelineLayout(std::move(pipelineLayout))
    {
        MLG_ASSERT(m_Shader, "Shader module is not valid");
        MLG_ASSERT(m_Sampler, "Sampler is not valid");
        MLG_ASSERT(m_BindGroupLayout, "Bind group layout is not valid");
        MLG_ASSERT(m_PipelineLayout, "Pipeline layout is not valid");
    }

    Result<> EnsurePipeline();
    Result<> EnsureInputsBindGroup();

    std::optional<Inputs> m_Inputs;
    std::optional<Outputs> m_Outputs;

    const GpuHelper* m_GpuHelper{ nullptr };
    wgpu::ShaderModule m_Shader;
    wgpu::Sampler m_Sampler;
    wgpu::BindGroupLayout m_BindGroupLayout;
    wgpu::PipelineLayout m_PipelineLayout;
    wgpu::BindGroup m_InputsBindGroup;
    wgpu::RenderPipeline m_Pipeline;
    wgpu::TextureFormat m_TargetFormat{ wgpu::TextureFormat::Undefined };
};