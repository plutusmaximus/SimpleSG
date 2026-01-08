#include "SDLRenderGraph.h"

#include "Error.h"
#include "scope_exit.h"

#include "SDLGPUDevice.h"

#include <SDL3/SDL_gpu.h>

SDLRenderGraph::SDLRenderGraph(SDLGPUDevice* gpuDevice)
    : m_GpuDevice(gpuDevice)
{
}

SDLRenderGraph::~SDLRenderGraph()
{
    WaitForFence();

    SDL_ReleaseGPUTexture(m_GpuDevice->Device, m_DepthBuffer);

    for(const auto& state: m_State)
    {
        eassert(!state.m_RenderFence, "Render fence must be null when destroying SDLRenderGraph");
    }
}

void
SDLRenderGraph::Add(const Mat44f& worldTransform, RefPtr<Model> model)
{
    std::vector<Mat44f> worldXForms;
    worldXForms.reserve(model->TransformNodes.size());

    // Precompute world transforms for all nodes
    for(const auto& node : model->TransformNodes)
    {
        if(node.ParentIndex >= 0)
        {
            worldXForms.emplace_back(
                worldXForms[node.ParentIndex].Mul(node.Transform));
        }
        else
        {
            worldXForms.emplace_back(worldTransform.Mul(node.Transform));
        }
    }

    for (const auto& meshInstance : model->MeshInstances)
    {
        const Mesh& mesh = model->Meshes[meshInstance.MeshIndex];

        const Material& mtl = mesh.Material;

        // Determine mesh group based on material properties

        MeshGroup* meshGrp;

        if(mtl.Key.Flags & MaterialFlags::Translucent)
        {
            meshGrp = &m_CurrentState->m_TranslucentMeshGroups[mtl.Key.Id];
        }
        else
        {
            meshGrp = &m_CurrentState->m_OpaqueMeshGroups[mtl.Key.Id];
        }

        XformMesh xformMesh
        {
            .WorldTransform = worldXForms[meshInstance.NodeIndex],
            .Model = model,
            .MeshInstance = mesh
        };

        meshGrp->emplace_back(xformMesh);
    }
}

Result<void>
SDLRenderGraph::Render(const Mat44f& camera, const Mat44f& projection)
{
    //Wait for the previous frame to complete
    WaitForFence();

    auto gpuDevice = m_GpuDevice->Device;
    auto window = m_GpuDevice->Window;

    SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(gpuDevice);

    expect(cmdBuf, SDL_GetError());

    auto renderPassResult = BeginRenderPass(cmdBuf);

    if(!renderPassResult)
    {
        SDL_CancelGPUCommandBuffer(cmdBuf);
        return std::unexpected(renderPassResult.error());
    }

    auto renderPass = renderPassResult.value();

    if(!renderPass)
    {
        //No render pass - likely window minimized.
        //This is not an error.
        SDL_CancelGPUCommandBuffer(cmdBuf);
        return {};
    }

    auto cleanup = scope_exit([&]()
    {
        SDL_EndGPURenderPass(renderPass);
        SDL_SubmitGPUCommandBuffer(cmdBuf);
    });

    // Use inverse of camera transform as view matrix
    const Mat44f viewXform = camera.Inverse();

    // Render opaque meshes first
    const MeshGroupCollection* meshGroups[] =
    {
        &m_CurrentState->m_OpaqueMeshGroups,
        &m_CurrentState->m_TranslucentMeshGroups
    };
    
    for(const auto meshGrpPtr : meshGroups)
    {
        for (auto& [mtlId, xmeshes] : *meshGrpPtr)
        {
            const Material& mtl = xmeshes[0].MeshInstance.Material;

            SDL_PushGPUVertexUniformData(cmdBuf, 1, &mtl.Color, sizeof(mtl.Color));

            //const int idx = m_MaterialDb->GetIndex(mtlId);

            //SDL_PushGPUVertexUniformData(m_CmdBuf, 2, &idx, sizeof(idx));
            const int idx = 0;
            SDL_PushGPUVertexUniformData(cmdBuf, 2, &idx, sizeof(idx));

            if (mtl.Albedo)
            {
                // Bind texture and sampler
                SDL_GPUTextureSamplerBinding samplerBinding
                {
                    .texture = mtl.Albedo.Get<SDLGpuTexture>()->Texture,
                    .sampler = mtl.Albedo.Get<SDLGpuTexture>()->Sampler
                };
                SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);
            }

            auto pipelineResult = m_GpuDevice->GetOrCreatePipeline(mtl);

            expect(pipelineResult, pipelineResult.error());

            SDL_BindGPUGraphicsPipeline(renderPass, pipelineResult.value());

            const Mat44f viewProj = projection.Mul(viewXform);

            for (auto& xmesh : xmeshes)
            {
                const Mat44f matrices[] =
                {
                    xmesh.WorldTransform,
                    viewProj.Mul(xmesh.WorldTransform)
                };

                // Send up the model and model-view-projection matrices
                SDL_PushGPUVertexUniformData(cmdBuf, 0, matrices, sizeof(matrices));

                const Mesh& mesh = xmesh.MeshInstance;

                SDL_GPUBufferBinding vertexBufferBinding
                {
                    .buffer = mesh.VtxBuffer.Get<SDLGpuVertexBuffer>()->Buffer,
                    .offset = mesh.VtxBuffer->ByteOffset
                };
                SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);

                SDL_GPUBufferBinding indexBufferBinding
                {
                    .buffer = mesh.IdxBuffer.Get<SDLGpuIndexBuffer>()->Buffer,
                    .offset = mesh.IdxBuffer->ByteOffset
                };

                static_assert(VERTEX_INDEX_BITS == 32 || VERTEX_INDEX_BITS == 16);

                const SDL_GPUIndexElementSize idxElSize =
                    (VERTEX_INDEX_BITS == 32)
                    ? SDL_GPU_INDEXELEMENTSIZE_32BIT
                    : SDL_GPU_INDEXELEMENTSIZE_16BIT;

                SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, idxElSize);

                SDL_DrawGPUIndexedPrimitives(renderPass, mesh.IndexCount, 1, 0, 0, 0);
            }
        }
    }

    SDL_EndGPURenderPass(renderPass);

    cleanup.release();

    SwapStates();

    eassert(!m_CurrentState->m_RenderFence, "Render fence should be null here");
    m_CurrentState->m_RenderFence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdBuf);

    expect(m_CurrentState->m_RenderFence, SDL_GetError());

    return {};
}

//private:

Result<SDL_GPURenderPass*>
SDLRenderGraph::BeginRenderPass(SDL_GPUCommandBuffer* cmdBuf)
{
    auto gpuDevice = m_GpuDevice->Device;
    auto window = m_GpuDevice->Window;

    SDL_GPUTexture* swapChainTexture;
    uint32_t windowW, windowH;
    expect(SDL_WaitAndAcquireGPUSwapchainTexture(cmdBuf, window, &swapChainTexture, &windowW, &windowH), SDL_GetError())

    if (!swapChainTexture)
    {
        //Perhaps window minimized
        //This is not an error, but callers must check for nullptr.
        return nullptr;
    }

    static constexpr float CLEAR_DEPTH = 1.0f;

    if (!m_DepthBuffer || m_DepthCreateInfo.width != windowW || m_DepthCreateInfo.height != windowH)
    {
        SDL_ReleaseGPUTexture(gpuDevice, m_DepthBuffer);
        m_DepthBuffer = nullptr;

        m_DepthCreateInfo.width = windowW;
        m_DepthCreateInfo.height = windowH;

        // Avoid D3D12 warning about not specifying clear depth.
        SDL_SetFloatProperty(m_DepthCreateInfo.props, SDL_PROP_GPU_TEXTURE_CREATE_D3D12_CLEAR_DEPTH_FLOAT, CLEAR_DEPTH);

        m_DepthBuffer = SDL_CreateGPUTexture(gpuDevice, &m_DepthCreateInfo);
        expect(m_DepthBuffer, SDL_GetError());
    }

    SDL_GPUColorTargetInfo colorTargetInfo
    {
        .texture = swapChainTexture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = {0, 0, 0, 0},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE
    };

    SDL_GPUDepthStencilTargetInfo depthTargetInfo
    {
        .texture = m_DepthBuffer,
        .clear_depth = CLEAR_DEPTH,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE
    };

    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(
        cmdBuf,
        &colorTargetInfo,
        1,
        &depthTargetInfo);

    expect(renderPass, SDL_GetError());    

    const auto screenBounds = m_GpuDevice->GetExtent();

    const SDL_GPUViewport viewport
    {
        0, 0, screenBounds.Width, screenBounds.Height, 0, CLEAR_DEPTH
    };
    SDL_SetGPUViewport(renderPass, &viewport);

    return renderPass;
}

void
SDLRenderGraph::WaitForFence()
{
    if(!m_CurrentState->m_RenderFence)
    {
        return;
    }

    bool success =
        SDL_WaitForGPUFences(
            m_GpuDevice->Device,
            true,
            &m_CurrentState->m_RenderFence,
            1);
            
    if(!success)
    {
        logError("Error waiting for render fence during SDLRenderGraph destruction: {}", SDL_GetError());
    }

    SDL_ReleaseGPUFence(m_GpuDevice->Device, m_CurrentState->m_RenderFence);
    m_CurrentState->m_RenderFence = nullptr;
}