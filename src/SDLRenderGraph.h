#pragma once

#include <vector>
#include <map>

#include "RenderGraph.h"
#include "Model.h"

#include <SDL3/SDL_gpu.h>

class Camera;
class SDLGPUDevice;

class SDLRenderGraph : public RenderGraph
{
public:

    virtual ~SDLRenderGraph() override;

    explicit SDLRenderGraph(SDLGPUDevice* gpuDevice);

    virtual void Add(const Mat44f& transform, RefPtr<Model> model) override;

    virtual std::expected<void, Error> Render(const Camera& camera) override;

    virtual void Reset() override;

private:
    
    std::expected<SDL_GPUGraphicsPipeline*, Error> CreatePipeline(SDL_GPUTextureFormat colorTargetFormat);

    struct XformMesh
    {
        const int TransformIdx;
        RefPtr<Mesh> Mesh;
    };

    std::vector<Mat44f> m_Transforms;

    RefPtr<SDLGPUDevice> m_GpuDevice;
    SDL_GPUTexture* m_DepthBuffer = nullptr;
    SDL_GPUGraphicsPipeline* m_Pipeline = nullptr;

    SDL_GPUTextureCreateInfo m_DepthCreateInfo
    {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
        .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
        .width = 0,
        .height = 0,
        .layer_count_or_depth = 1,
        .num_levels = 1
    };


    std::map<MaterialId, std::vector<XformMesh>> m_MeshGroups;
};