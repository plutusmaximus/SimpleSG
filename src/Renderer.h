#pragma once

#include "SceneTypes.h"
#include "Result.h"

#include <webgpu/webgpu_cpp.h>

class FileFetcher;
class GpuColorPass;
class GpuCompositorPass;
class GpuHelper;

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

    Result<> Startup(GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> Shutdown();

    Result<> Render(const GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        const Camera& camera,
        const TrTransformf& cameraXForm,
        const Scene& scene,
        const PropKit& propKit);

    Result<> Render(const GpuHelper& gpuHelper,
        GpuColorPass& colorPass,
        const Camera& camera,
        const TrTransformf& cameraXForm,
        const Scene& scene,
        const PropKit& propKit);

    Result<wgpu::Texture> GetTarget() const;

    Result<> Composite(
        const wgpu::Device& gpuDevice, FileFetcher& fileFetcher, const wgpu::Texture& target);

    Result<> Composite(const wgpu::Device& gpuDevice,
        GpuCompositorPass& compositorPass,
        const wgpu::Texture& target) const;

private:
    Result<wgpu::RenderPassEncoder> BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder);

    Result<> EnsureColorTarget(const wgpu::Device& gpuDevice,
        const uint32_t width,
        const uint32_t height,
        wgpu::TextureFormat targetFormat);

    Result<> EnsureColorPipeline(const wgpu::Device& gpuDevice, FileFetcher& fileFetcher);

    Result<> EnsureCompositorPipeline(const wgpu::Device& gpuDevice,
        FileFetcher& fileFetcher,
        const wgpu::TextureFormat targetFormat);

    Result<> CreateTransformPipeline(const wgpu::Device& gpuDevice, FileFetcher& fileFetcher);

    Result<> TransformNodes(const wgpu::Device& gpuDevice,
        const wgpu::CommandEncoder& cmdEncoder,
        const TrTransformf& cameraXForm,
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