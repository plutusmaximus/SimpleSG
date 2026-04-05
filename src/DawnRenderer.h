#pragma once

#include "Renderer.h"
#include "Model.h"

#include <unordered_map>
#include <webgpu/webgpu_cpp.h>

class DawnGpuDevice;
class GpuTexture;

class DawnRenderer : public Renderer
{
public:

    DawnRenderer() = delete;
    DawnRenderer(const DawnRenderer&) = delete;
    DawnRenderer& operator=(const DawnRenderer&) = delete;
    DawnRenderer(DawnRenderer&&) = delete;
    DawnRenderer& operator=(DawnRenderer&&) = delete;

    ~DawnRenderer() override;

    static Result<DawnRenderer*> Create(DawnGpuDevice* gpuDevice);

    static Result<> Destroy(DawnRenderer* renderer);

    Result<> Render(const Mat44f& camera,
        const Mat44f& projection,
        const Model* model,
        RenderCompositor* compositor) override;

    Result<> Render(const Mat44f& camera,
        const Mat44f& projection,
        const SceneKit& sceneKit,
        RenderCompositor* compositor) override;

private:

    explicit DawnRenderer(DawnGpuDevice* gpuDevice);

    Result<wgpu::RenderPassEncoder> BeginRenderPass(wgpu::CommandEncoder cmdEncoder);

    /// @brief Copy the color target to the swapchain texture.
    Result<> CopyColorTargetToSwapchain(wgpu::CommandEncoder cmdEncoder,
        wgpu::TextureView target);

    Result<> CreateColorAndDepthTargets();

    Result<> CreateColorPipeline();

    Result<> CreateBltPipeline();

    Result<> CreateTransformPipeline();

    Result<wgpu::ShaderModule> CreateShader(const char* path);

    Result<> TransformNodes(wgpu::CommandEncoder cmdEncoder,
        const Mat44f& camera,
        const Mat44f& projection,
        const SceneKit& sceneKit);

    /// Get or create the default texture.
    /// The default texture is used when a material does not have a base texture.
    Result<GpuTexture*> GetDefaultBaseTexture();

    DawnGpuDevice* const m_GpuDevice;
    wgpu::Limits m_GpuLimits;
    wgpu::Texture m_ColorTarget;
    wgpu::TextureView m_ColorTargetView;
    wgpu::Sampler m_ColorTargetSampler;
    wgpu::Texture m_DepthTarget;
    wgpu::TextureView m_DepthTargetView;

    GpuTexture* m_DefaultBaseTexture{nullptr};

    struct Pipeline
    {
        wgpu::ShaderModule VertexShader;
        wgpu::ShaderModule FragmentShader;
        wgpu::PipelineLayout PipelineLayout;
        wgpu::BindGroup BindGroup0;
        wgpu::BindGroup BindGroup1;
        wgpu::BindGroup BindGroup2;
        wgpu::RenderPipeline Pipeline;
    };

    // Pipeline for rendering to the color target texture.
    Pipeline m_ColorPipeline;

    // Pipeline to BLT the color target to the swap chain.
    Pipeline m_BltPipeline;

    struct TransformBuffers
    {
        size_t TransformCount{0};
        wgpu::Buffer ClipSpaceBuf;
        wgpu::Buffer ViewProjBuf;
        wgpu::BindGroup BindGroup1;
        wgpu::BindGroup BindGroup2;
    };

    TransformBuffers m_TransformBuffers;

    wgpu::ShaderModule m_TransformShader;
    wgpu::ComputePipeline m_TransformPipeline;
};