#include "SdlRenderGraph.h"

#include "Camera.h"

#include "Assert.h"

#include <SDL3/SDL_gpu.h>

SdlRenderGraph::SdlRenderGraph(RefPtr<MaterialDb> materialDb, SDL_GPUCommandBuffer* cmdBuf, SDL_GPURenderPass* renderPass)
    : m_MaterialDb(materialDb)
    , m_CmdBuf(cmdBuf)
    , m_RenderPass(renderPass)
{
}

void
SdlRenderGraph::Add(const Mat44f& transform, RefPtr<Model> model)
{
    const int xfomIdx = static_cast<int>(m_Transforms.size());
    m_Transforms.push_back(transform);

    for (const auto& mesh : model->Meshes)
    {
        const MaterialId mtlId = mesh->MaterialId;

        if (!Verify(m_MaterialDb->Contains(mtlId)))
        {
            continue;
        }

        m_MeshGroups[mtlId].push_back({ xfomIdx, mesh });
    }
}

void
SdlRenderGraph::Render(const Camera& camera)
{
    for (auto& [mtlId, xmeshes] : m_MeshGroups)
    {
        auto mtl = m_MaterialDb->GetMaterial(mtlId);

        SDL_PushGPUVertexUniformData(m_CmdBuf, 1, &mtl->Color, sizeof(mtl->Color));

        //const int idx = m_MaterialDb->GetIndex(mtlId);

        //SDL_PushGPUVertexUniformData(m_CmdBuf, 2, &idx, sizeof(idx));
        const int idx = 0;
        SDL_PushGPUVertexUniformData(m_CmdBuf, 2, &idx, sizeof(idx));

        // Bind texture and sampler
        SDL_GPUTextureSamplerBinding samplerBinding
        {
            .texture = mtl->Albedo->Get(),
            .sampler = mtl->AlbedoSampler->Get()
        };
        SDL_BindGPUFragmentSamplers(m_RenderPass, 0, &samplerBinding, 1);

        for (auto& xmesh : xmeshes)
        {
            const Mat44 xform = m_Transforms[xmesh.TransformIdx].Mul(camera.ViewProj());

            SDL_PushGPUVertexUniformData(m_CmdBuf, 0, &xform, sizeof(xform));

            SDL_GPUBufferBinding vertexBufferBinding
            {
                .buffer = xmesh.Mesh->VertexBuffer->Get(),
                .offset = 0
            };
            SDL_BindGPUVertexBuffers(m_RenderPass, 0, &vertexBufferBinding, 1);

            SDL_GPUBufferBinding indexBufferBinding
            {
                .buffer = xmesh.Mesh->IndexBuffer->Get(),
                .offset = 0
            };
            SDL_BindGPUIndexBuffer(m_RenderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

            /*// Bind texture and sampler
            SDL_GPUTextureSamplerBinding samplerBinding
            {
                .texture = texture,
                .sampler = sampler
            };
            SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);*/

            SDL_DrawGPUIndexedPrimitives(m_RenderPass, xmesh.Mesh->IndexCount, 1, xmesh.Mesh->IndexOffset, 0, 0);
        }
    }
}

void
SdlRenderGraph::Reset()
{
    m_Transforms.clear();
    for (auto& [mtlId, meshes] : m_MeshGroups)
    {
        meshes.clear();
    }
}