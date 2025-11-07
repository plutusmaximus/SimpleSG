#pragma once

#include <vector>
#include <map>

#include "RenderGraph.h"
#include "SceneNodes.h"

#include <SDL3/SDL_gpu.h>

class SDLGPUDevice;

class SDLRenderGraph : public RenderGraph
{
public:

    virtual ~SDLRenderGraph() override;

    explicit SDLRenderGraph(SDLGPUDevice* gpuDevice);

    virtual void Add(const Mat44f& viewTransform, RefPtr<ModelNode> model) override;

    virtual Result<void> Render(const Mat44f& camera, const Mat44f& projection) override;

    virtual void Reset() override;

private:

    struct XformMesh
    {
        const Mat44f& ViewTransform;
        RefPtr<Mesh> Mesh;
    };

    std::list<Mat44f> m_ViewTransforms;

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


    std::map<MaterialId, std::list<XformMesh>> m_MeshGroups;
};