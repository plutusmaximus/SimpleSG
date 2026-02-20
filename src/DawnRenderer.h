#pragma once

#include "Renderer.h"
#include "Model.h"

#include <unordered_map>
#include <webgpu/webgpu_cpp.h>

class DawnGpuDevice;
class GpuTexture;
struct ImGuiContext;

class DawnRenderer : public Renderer
{
public:

    DawnRenderer() = delete;
    DawnRenderer(const DawnRenderer&) = delete;
    DawnRenderer& operator=(const DawnRenderer&) = delete;
    DawnRenderer(DawnRenderer&&) = delete;
    DawnRenderer& operator=(DawnRenderer&&) = delete;

    DawnRenderer(DawnGpuDevice* gpuDevice, GpuPipeline* pipeline);

    ~DawnRenderer() override;

    Result<void> BeginFrame() override;

    void AddModel(const Mat44f& worldTransform, const Model* model) override;

    Result<void> Render(const Mat44f& camera, const Mat44f& projection) override;

private:

    friend class DawnGpuDevice;

    Result<wgpu::RenderPassEncoder> BeginRenderPass(wgpu::CommandEncoder cmdEncoder);

    struct XformMesh
    {
        const Mat44f WorldTransform;
        const Model* Model;
        const Mesh& MeshInstance;
    };

    using MeshGroup = std::vector<XformMesh>;
    using MeshGroupCollection = std::unordered_map<MaterialId, MeshGroup>;

    struct State
    {
        void Clear()
        {
            //DO NOT SUBMIT
            //eassert(!m_RenderFence, "Render fence must be null when clearing state");
            m_OpaqueMeshGroups.clear();
            m_TranslucentMeshGroups.clear();
            m_MeshCount = 0;
        }

        MeshGroupCollection m_TranslucentMeshGroups;

        MeshGroupCollection m_OpaqueMeshGroups;

        size_t m_MeshCount = 0;

        //DO NOT SUBMIT
        //SDL_GPUFence* m_RenderFence = nullptr;
    };

    //void WaitForFence();

    void SwapStates();

    /// @brief Copy the color target to the swapchain texture.
    Result<void> CopyColorTargetToSwapchain(wgpu::CommandEncoder cmdEncoder,
        wgpu::TextureView swapchainTextureView);

    Result<GpuVertexShader*> GetCopyColorTargetVertexShader();
    Result<GpuFragmentShader*> GetCopyColorTargetFragmentShader();
    Result<wgpu::RenderPipeline> GetCopyColorTargetPipeline();

    /// Get or create the default texture.
    /// The default texture is used when a material does not have a base texture.
    Result<GpuTexture*> GetDefaultBaseTexture();

    Result<void> InitGui();
    Result<void> RenderGui(wgpu::CommandEncoder cmdEncoder, wgpu::TextureView swapchainTextureView);

    DawnGpuDevice* const m_GpuDevice;
    GpuPipeline* m_Pipeline{ nullptr };
    GpuColorTarget* m_ColorTarget{ nullptr };
    GpuDepthTarget* m_DepthTarget{ nullptr };

    State m_State[2];
    State* m_CurrentState = &m_State[0];
    GpuTexture* m_DefaultBaseTexture{nullptr};

    /// These are used for copying the color target to the swapchain texture.
    GpuVertexShader* m_CopyTextureVertexShader{ nullptr };
    GpuFragmentShader* m_CopyTextureFragmentShader{ nullptr };
    wgpu::RenderPipeline m_CopyTexturePipeline;
    wgpu::BindGroupLayout m_CopyTextureBindGroupLayout;
    wgpu::BindGroup m_CopyTextureBindGroup;

    std::unordered_map<MaterialId, wgpu::BindGroup> m_MaterialBindGroups;

    size_t m_SizeofTransformBuffer{0};
    wgpu::Buffer m_WorldAndProjBuf;
    wgpu::Buffer m_MaterialColorBuf;
    wgpu::BindGroup m_VertexShaderBindGroup;

    ImGuiContext* m_ImGuiContext{ nullptr };

    unsigned m_BeginFrameCount{0};
    unsigned m_RenderCount{0};
};