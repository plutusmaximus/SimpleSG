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

    Result<> Render(const Mat44f& camera,
        const Mat44f& projection,
        const Model* model,
        RenderCompositor* compositor) override;

private:

    friend class DawnGpuDevice;

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

    Result<> UpdateXformBuffer(wgpu::CommandEncoder cmdEncoder,
        const Mat44f& camera,
        const Mat44f& projection,
        const Model* model);

    /// Get or create the default texture.
    /// The default texture is used when a material does not have a base texture.
    Result<GpuTexture*> GetDefaultBaseTexture();

    DawnGpuDevice* const m_GpuDevice;
    wgpu::Limits m_GpuLimits;
    GpuColorTarget* m_ColorTarget{ nullptr };
    GpuDepthTarget* m_DepthTarget{ nullptr };

    GpuTexture* m_DefaultBaseTexture{nullptr};

    struct Pipeline
    {
        wgpu::ShaderModule VertexShader;
        wgpu::ShaderModule FragmentShader;
        wgpu::BindGroupLayout BindGroup0Layout;
        wgpu::BindGroupLayout BindGroup1Layout;
        wgpu::BindGroupLayout BindGroup2Layout;
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
        bool NeedsRebuild(const size_t size) const
        {
            return size > SizeofTransformBuffer || !ClipSpaceBuf ||
                   !ViewProjBuf || !BindGroups[0] || !BindGroups[1] || !BindGroups[2];
        }

        size_t SizeofTransformBuffer{0};
        wgpu::Buffer ClipSpaceBuf;
        wgpu::Buffer ViewProjBuf;
        wgpu::BindGroup BindGroups[3];
    };

    TransformBuffers m_TransformBuffers;

    wgpu::ShaderModule m_TransformShader;
    wgpu::ComputePipeline m_TransformPipeline;
};