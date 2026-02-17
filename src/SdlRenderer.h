#pragma once

#include "Renderer.h"
#include "Model.h"

#include <map>

class SdlGpuDevice;
class GpuTexture;
struct SDL_GPURenderPass;
struct SDL_GPUCommandBuffer;
struct SDL_GPUFence;
struct SDL_GPUGraphicsPipeline;

class SdlRenderer : public Renderer
{
public:

    SdlRenderer() = delete;
    SdlRenderer(const SdlRenderer&) = delete;
    SdlRenderer& operator=(const SdlRenderer&) = delete;
    SdlRenderer(SdlRenderer&&) = delete;
    SdlRenderer& operator=(SdlRenderer&&) = delete;

    SdlRenderer(SdlGpuDevice* gpuDevice, GpuPipeline* pipeline);

    virtual ~SdlRenderer() override;

    virtual void Add(const Mat44f& worldTransform, const Model* model) override;

    virtual Result<void> Render(const Mat44f& camera, const Mat44f& projection) override;

private:

    friend class SdlGpuDevice;

    Result<SDL_GPURenderPass*> BeginRenderPass(SDL_GPUCommandBuffer* cmdBuf);

    struct XformMesh
    {
        const Mat44f WorldTransform;
        const Model* Model;
        const Mesh& MeshInstance;
    };

    using MeshGroup = std::vector<XformMesh>;
    using MeshGroupCollection = std::map<MaterialId, MeshGroup>;

    struct State
    {
        void Clear()
        {
            eassert(!m_RenderFence, "Render fence must be null when clearing state");
            m_OpaqueMeshGroups.clear();
            m_TranslucentMeshGroups.clear();
        }

        MeshGroupCollection m_TranslucentMeshGroups;

        MeshGroupCollection m_OpaqueMeshGroups;

        SDL_GPUFence* m_RenderFence = nullptr;
    };

    void WaitForFence();

    void SwapStates();

    /// @brief Copy the color target to the swapchain texture.
    Result<void> CopyColorTargetToSwapchain(SDL_GPUCommandBuffer* cmdBuf);

    /// Get or create the default texture.
    /// The default texture is used when a material does not have a base texture.
    Result<GpuTexture*> GetDefaultBaseTexture();

    SdlGpuDevice* const m_GpuDevice;
    GpuPipeline* m_Pipeline{ nullptr };
    GpuColorTarget* m_ColorTarget{ nullptr };
    GpuDepthTarget* m_DepthTarget{ nullptr };

    State m_State[2];
    State* m_CurrentState = &m_State[0];
    GpuTexture* m_DefaultBaseTexture{nullptr};

    /// These are used for copying the color target to the swapchain texture.
    GpuVertexShader* m_CopyTextureVertexShader{ nullptr };
    GpuFragmentShader* m_CopyTextureFragmentShader{ nullptr };
    SDL_GPUGraphicsPipeline* m_CopyTexturePipeline{ nullptr };
};