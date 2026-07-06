#pragma once

#include "SceneTypes.h"
#include "Result.h"

#include <webgpu/webgpu_cpp.h>

template<typename T>
class Mat44;
using Mat44f = Mat44<float>;
class Camera;
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

    Result<> Render(const Camera& camera,
        const Posef& cameraXForm,
        const Scene& scene,
        const PropKit& propKit);

    Result<> GetTarget(wgpu::Texture& outTexture, wgpu::TextureView& outTextureView) const;

    Result<> Composite(const wgpu::Texture& target);

private:

    Result<wgpu::RenderPassEncoder> BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder);

    Result<> EnsureColorTarget(const uint32_t width, const uint32_t height);

    Result<> EnsureColorPipeline(const wgpu::TextureFormat targetFormat,
        const wgpu::TextureFormat depthFormat);

    Result<> EnsureCompositorPipeline(const wgpu::TextureFormat targetFormat);

    Result<> CreateTransformPipeline();

    Result<> TransformNodes(const wgpu::CommandEncoder& cmdEncoder,
        const Posef& cameraXForm,
        const Camera& camera,
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
        wgpu::TextureFormat TargetFormat{ wgpu::TextureFormat::Undefined };
        wgpu::TextureFormat DepthFormat{ wgpu::TextureFormat::Undefined };
    };

    struct TransformPipelineResources
    {
        wgpu::ShaderModule Shader;
        wgpu::PipelineLayout Layout;
    };

    struct CompositorPipelineResources
    {
        wgpu::ShaderModule Shader;
        wgpu::PipelineLayout Layout;
        wgpu::TextureFormat TargetFormat{ wgpu::TextureFormat::Undefined };
    };

    ColorTargetResources m_ColorTargetResources;

    // Pipeline for rendering to the color target texture.
    ColorPipelineResources m_ColorPipelineResources;
    wgpu::RenderPipeline m_ColorPipeline;

    // Pipeline for computing world transforms on the GPU.
    TransformPipelineResources m_TransformPipelineResources;
    wgpu::ComputePipeline m_TransformPipeline;

    // Pipeline to composite the color target to the swap chain.
    CompositorPipelineResources m_CompositorPipelineResources;
    wgpu::RenderPipeline m_CompositorPipeline;

    std::vector<MeshInstance> m_VisibleMeshes;

    bool m_Initialized{false};
};