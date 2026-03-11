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

    void AddModel(const Mat44f& worldTransform, const Model* model) override;

    Result<> Render(const Mat44f& camera,
        const Mat44f& projection,
        const Model* models,
        const size_t modelCount,
        RenderCompositor* compositor) override;

private:

    friend class DawnGpuDevice;

    explicit DawnRenderer(DawnGpuDevice* gpuDevice);

    Result<wgpu::RenderPassEncoder> BeginRenderPass(wgpu::CommandEncoder cmdEncoder);

    struct State
    {
        void Clear()
        {
            m_Meshes.clear();
            m_Transforms.clear();
            m_Materials.clear();
            m_MeshCount = 0;
        }

        std::vector<const Mesh*> m_Meshes;

        std::vector<Mat44f> m_Transforms;

        std::vector<TransformIndex> m_MeshToTransformMap;

        std::vector<const GpuMaterial*> m_Materials;

        size_t m_MeshCount = 0;
    };

    //void WaitForFence();

    void SwapStates();

    /// @brief Copy the color target to the swapchain texture.
    Result<> CopyColorTargetToSwapchain(wgpu::CommandEncoder cmdEncoder,
        wgpu::TextureView target);

    Result<wgpu::RenderPipeline> GetColorPipeline();

    Result<wgpu::RenderPipeline> GetCopyColorTargetPipeline();

    Result<wgpu::ComputePipeline> GetTransformPipeline();

    Result<> CreateLayouts();

    Result<wgpu::ShaderModule> CreateShader(const char* path);

    Result<> UpdateXformBuffer(wgpu::CommandEncoder cmdEncoder,
        const Mat44f& camera,
        const Mat44f& projection,
        const Model* models,
        const size_t modelCount);

    /// Get or create the default texture.
    /// The default texture is used when a material does not have a base texture.
    Result<GpuTexture*> GetDefaultBaseTexture();

    DawnGpuDevice* const m_GpuDevice;
    wgpu::Limits m_GpuLimits;
    GpuColorTarget* m_ColorTarget{ nullptr };
    GpuDepthTarget* m_DepthTarget{ nullptr };

    State m_State[2];
    State* m_CurrentState = &m_State[0];
    GpuTexture* m_DefaultBaseTexture{nullptr};

    struct Pipeline
    {
        wgpu::ShaderModule VertexShader;
        wgpu::ShaderModule FragmentShader;
        wgpu::BindGroupLayout VsBindGroupLayout;
        wgpu::BindGroupLayout FsBindGroupLayout;
        wgpu::PipelineLayout PipelineLayout;
        wgpu::BindGroup VsBindGroup;
        wgpu::BindGroup FsBindGroup;
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
            return size > SizeofTransformBuffer || !WorldSpaceBuf || !ClipSpaceBuf ||
                   !ViewProjBuf || !BindGroup0;
        }

        size_t SizeofTransformBuffer{0};
        wgpu::Buffer WorldSpaceBuf;
        wgpu::Buffer ClipSpaceBuf;
        wgpu::Buffer ViewProjBuf;
        wgpu::BindGroup BindGroup0;
    };

    TransformBuffers m_TransformBuffers;

    wgpu::ShaderModule m_TransformShader;
    wgpu::ComputePipeline m_TransformPipeline;
};