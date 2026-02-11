#pragma once

#include "RenderGraph.h"
#include "Model.h"

#include <SDL3/SDL_gpu.h>
#include <map>

class SdlGpuDevice;
class GpuTexture;
struct SDL_GPURenderPass;
struct SDL_GPUCommandBuffer;
struct SDL_GPUFence;

class SdlRenderGraph : public RenderGraph
{
public:

    SdlRenderGraph() = delete;
    SdlRenderGraph(const SdlRenderGraph&) = delete;
    SdlRenderGraph& operator=(const SdlRenderGraph&) = delete;
    SdlRenderGraph(SdlRenderGraph&&) = delete;
    SdlRenderGraph& operator=(SdlRenderGraph&&) = delete;

    virtual ~SdlRenderGraph() override;

    virtual void Add(const Mat44f& worldTransform, const Model* model) override;

    virtual Result<void> Render(const Mat44f& camera, const Mat44f& projection) override;

private:

    friend class SdlGpuDevice;

    explicit SdlRenderGraph(SdlGpuDevice* gpuDevice);

    Result<SDL_GPURenderPass*> BeginRenderPass(SDL_GPUCommandBuffer* cmdBuf);

    struct XformMesh
    {
        const Mat44f WorldTransform;
        const Model* Model;
        const Mesh& MeshInstance;
    };

    SdlGpuDevice* const m_GpuDevice;
    SDL_GPUTexture* m_DepthBuffer = nullptr;

    SDL_GPUTextureCreateInfo m_DepthCreateInfo
    {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = 0,
        .height = 0,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .props = SDL_CreateProperties()
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

    void SwapStates()
    {
        eassert(!m_CurrentState->m_RenderFence, "Current state's render fence must be null when swapping states");

        if (m_CurrentState == &m_State[0])
        {
            m_CurrentState = &m_State[1];
        }
        else
        {
            m_CurrentState = &m_State[0];
        }

        m_CurrentState->Clear();
    }

    /// Get or create the default texture.
    /// The default texture is used when a material does not have an albedo texture.
    Result<GpuTexture*> GetDefaultAlbedoTexture();

    State m_State[2];
    State* m_CurrentState = &m_State[0];
    GpuTexture* m_DefaultAlbedoTexture{nullptr};
};