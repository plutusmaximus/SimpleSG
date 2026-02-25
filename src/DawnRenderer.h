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

    ~DawnRenderer() override;

    Result<void> NewFrame() override;

    void AddModel(const Mat44f& worldTransform, const Model* model) override;

    Result<void> Render(
        const Mat44f& camera, const Mat44f& projection, RenderCompositor* compositor) override;

private:

    friend class DawnGpuDevice;

    explicit DawnRenderer(DawnGpuDevice* gpuDevice);

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
        wgpu::TextureView target);

    Result<GpuVertexShader*> GetColorVertexShader();
    Result<GpuFragmentShader*> GetColorFragmentShader();
    Result<wgpu::RenderPipeline> GetColorPipeline();

    Result<GpuVertexShader*> GetCopyColorTargetVertexShader();
    Result<GpuFragmentShader*> GetCopyColorTargetFragmentShader();
    Result<wgpu::RenderPipeline> GetCopyColorTargetPipeline();

    Result<GpuVertexShader*> CreateVertexShader(const char* path);
    Result<GpuFragmentShader*> CreateFragmentShader(const char* path);

    /// Get or create the default texture.
    /// The default texture is used when a material does not have a base texture.
    Result<GpuTexture*> GetDefaultBaseTexture();

    Result<void> InitGui();
    Result<void> RenderGui(wgpu::CommandEncoder cmdEncoder, wgpu::TextureView target);

    DawnGpuDevice* const m_GpuDevice;
    wgpu::Limits m_GpuLimits;
    GpuColorTarget* m_ColorTarget{ nullptr };
    GpuDepthTarget* m_DepthTarget{ nullptr };

    State m_State[2];
    State* m_CurrentState = &m_State[0];
    GpuTexture* m_DefaultBaseTexture{nullptr};

    /// These are used for rendering to the color target texture.
    GpuVertexShader* m_ColorVertexShader{ nullptr };
    GpuFragmentShader* m_ColorFragmentShader{ nullptr };
    wgpu::RenderPipeline m_ColorPipeline;
    wgpu::BindGroupLayout m_VsBindGroupLayout;
    wgpu::BindGroupLayout m_FsBindGroupLayout;

    /// These are used for copying the color target to the swapchain texture.
    GpuVertexShader* m_CopyTextureVertexShader{ nullptr };
    GpuFragmentShader* m_CopyTextureFragmentShader{ nullptr };
    wgpu::RenderPipeline m_CopyTexturePipeline;
    wgpu::BindGroupLayout m_CopyTextureBindGroupLayout;
    wgpu::BindGroup m_CopyTextureBindGroup;

    size_t m_SizeofTransformBuffer{0};
    wgpu::Buffer m_WorldAndProjBuf;
    wgpu::BindGroup m_VertexShaderBindGroup;

    ImGuiContext* m_ImGuiContext{ nullptr };

    unsigned m_NewFrameCount{0};
    unsigned m_RenderCount{0};
};