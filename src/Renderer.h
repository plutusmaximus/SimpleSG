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

    Result<> Present(Compositor& compositor) const;

    Result<> RefreshColorTargetResources(const uint32_t width, const uint32_t height);

    Result<> CreateColorPipeline();

    Result<> CreatePresentPipeline();

    Result<> CreateTransformPipeline();

    Result<> TransformNodes(const wgpu::CommandEncoder& cmdEncoder,
        const TrsTransformf& camera,
        const Projection& projection,
        const Scene& scene) const;

    wgpu::Limits m_GpuLimits;

    struct ColorTargetResources
    {
        wgpu::Texture Target;
        wgpu::TextureView TargetView;
        wgpu::Texture DepthTarget;
        wgpu::TextureView DepthTargetView;
        wgpu::BindGroup BindGroup;
        wgpu::Sampler Sampler;
    };

    struct ColorPipelineResources
    {
        wgpu::ShaderModule Shader;
        wgpu::PipelineLayout Layout;
    };

    struct TransformPipelineResources
    {
        wgpu::ShaderModule Shader;
        wgpu::PipelineLayout Layout;
    };

    struct PresentPipelineResources
    {
        wgpu::ShaderModule Shader;
        wgpu::PipelineLayout Layout;
    };

    ColorTargetResources m_ColorTargetResources;

    // Pipeline for rendering to the color target texture.
    ColorPipelineResources m_ColorPipelineResources;
    wgpu::RenderPipeline m_ColorPipeline;

    // Pipeline for computing world transforms on the GPU.
    TransformPipelineResources m_TransformPipelineResources;
    wgpu::ComputePipeline m_TransformPipeline;

    // Pipeline to present the color target to the swap chain.
    PresentPipelineResources m_PresentPipelineResources;
    wgpu::RenderPipeline m_PresentPipeline;

    bool m_Initialized{false};
};