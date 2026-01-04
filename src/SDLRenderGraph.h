#pragma once

#include <map>

#include "RenderGraph.h"
#include "Model.h"

#include <SDL3/SDL_gpu.h>
#include <deque>
#include <unordered_map>

class SDLGPUDevice;
struct SDL_GPURenderPass;
struct SDL_GPUCommandBuffer;

class SDLRenderGraph : public RenderGraph
{
public:

    virtual ~SDLRenderGraph() override;

    explicit SDLRenderGraph(SDLGPUDevice* gpuDevice);

    virtual void Add(const Mat44f& worldTransform, RefPtr<Model> model) override;

    virtual Result<void> Render(const Mat44f& camera, const Mat44f& projection) override;

    virtual void Reset() override;

private:

    Result<SDL_GPURenderPass*> BeginRenderPass(SDL_GPUCommandBuffer* cmdBuf);

    struct XformMesh
    {
        const Mat44f WorldTransform;
        const RefPtr<Model> Model;
        const int MeshInstanceIndex;
    };

    RefPtr<SDLGPUDevice> m_GpuDevice;
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

    using MeshGroup = std::deque<XformMesh>;
    using MeshGroupCollection = std::map<MaterialId, MeshGroup>;

    std::unordered_map<MaterialId, Material> m_MaterialCache;

    MeshGroupCollection m_TranslucentMeshGroups;

    MeshGroupCollection m_OpaqueMeshGroups;
};