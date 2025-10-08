#pragma once

#include <vector>
#include <map>

#include "RenderGraph.h"
#include "Model.h"

struct SDL_GPUCommandBuffer;
struct SDL_GPURenderPass;
class Camera;

class SdlRenderGraph : public RenderGraph
{
public:

    virtual ~SdlRenderGraph() override {}

    SdlRenderGraph(RefPtr<MaterialDb> materialDb, SDL_GPUCommandBuffer* cmdBuf, SDL_GPURenderPass* renderPass);

    virtual void Add(const Mat44f& transform, RefPtr<Model> model) override;

    virtual void Render(const Camera& camera) override;

    virtual void Reset() override;

private:

    struct XformMesh
    {
        const int TransformIdx;
        RefPtr<Mesh> Mesh;
    };

    RefPtr<MaterialDb> m_MaterialDb;

    SDL_GPUCommandBuffer* m_CmdBuf;

    SDL_GPURenderPass* m_RenderPass;

    std::vector<Mat44f> m_Transforms;

    std::map<MaterialId, std::vector<XformMesh>> m_MeshGroups;
};