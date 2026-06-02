#pragma once

#include "Result.h"

#include "WebgpuHelper.h"

class Compositor;
template<typename T>
class Mat44;
using Mat44f = Mat44<float>;
class Projection;
class PropKit;
class Scene;

class Renderer
{
public:

    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    ~Renderer()
    {
        Shutdown();
    }

    Result<> Startup();

    Result<> Shutdown();

    Result<> Render(const TrsTransformf& camera,
        const Projection& projection,
        const Scene& scene,
        const PropKit& propKit,
        Compositor& compositor);

private:

    Result<wgpu::RenderPassEncoder> BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder);

    Result<> Present(Compositor& compositor);

    Result<> CreateColorAndDepthTargets();

    Result<> CreateColorPipeline();

    Result<> CreatePresentPipeline();

    Result<> CreateTransformPipeline();

    Result<wgpu::ShaderModule> CreateShader(const char* path);

    Result<> TransformNodes(const wgpu::CommandEncoder& cmdEncoder,
        const TrsTransformf& camera,
        const Projection& projection,
        const Scene& scene);

    wgpu::Limits m_GpuLimits;
    wgpu::Texture m_ColorTarget;
    wgpu::TextureView m_ColorTargetView;
    wgpu::Sampler m_ColorTargetSampler;
    wgpu::Texture m_DepthTarget;
    wgpu::TextureView m_DepthTargetView;

    struct ColorPipeline
    {
        wgpu::ShaderModule Shader;
        wgpu::PipelineLayout Layout;
        wgpu::RenderPipeline Pipeline;
    };

    struct TransformPipeline
    {
        wgpu::ShaderModule Shader;
        wgpu::PipelineLayout Layout;
        wgpu::ComputePipeline Pipeline;
    };

    struct PresentPipeline
    {
        wgpu::ShaderModule Shader;
        wgpu::PipelineLayout Layout;
        wgpu::BindGroup BindGroup0;
        wgpu::RenderPipeline Pipeline;
    };

    // Pipeline for rendering to the color target texture.
    ColorPipeline m_ColorPipeline;

    // Pipeline for computing world transforms on the GPU.
    TransformPipeline m_TransformPipeline;

    // Pipeline to present the color target to the swap chain.
    PresentPipeline m_PresentPipeline;

    bool m_Initialized{false};
};