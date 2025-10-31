#include "SDLRenderGraph.h"

#include "Camera.h"

#include "Error.h"

#include "AutoDeleter.h"
#include "Assert.h"

#include "SDLGPUDevice.h"

#include <SDL3/SDL_gpu.h>

SDLRenderGraph::SDLRenderGraph(SDLGPUDevice* gpuDevice)
    : m_GpuDevice(gpuDevice)
{
}

SDLRenderGraph::~SDLRenderGraph()
{
    SDL_ReleaseGPUTexture(m_GpuDevice->m_GpuDevice, m_DepthBuffer);
}

void
SDLRenderGraph::Add(const Mat44f& transform, RefPtr<Model> model)
{
    const int xfomIdx = static_cast<int>(m_Transforms.size());
    m_Transforms.push_back(transform);

    for (const auto& mesh : model->Meshes)
    {
        const MaterialId mtlId = mesh->MaterialId;

        m_MeshGroups[mtlId].push_back({ xfomIdx, mesh });
    }
}

Result<void>
SDLRenderGraph::Render(const Camera& camera)
{
    auto gpuDevice = m_GpuDevice->m_GpuDevice;
    auto window = m_GpuDevice->m_Window;

    SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(gpuDevice);

    expect(cmdBuf, SDL_GetError());

    auto adCmdBuf = AutoDeleter(SDL_CancelGPUCommandBuffer, cmdBuf);

    SDL_GPUTexture* swapChainTexture;
    uint32_t windowW, windowH;
    expect(SDL_WaitAndAcquireGPUSwapchainTexture(cmdBuf, window, &swapChainTexture, &windowW, &windowH), SDL_GetError())

    if (nullptr == swapChainTexture)
    {
        //Perhaps window minimized
        std::this_thread::yield();
        return {};
    }

    static constexpr float CLEAR_DEPTH = 1.0f;

    if (!m_DepthBuffer || m_DepthCreateInfo.width != windowW || m_DepthCreateInfo.height != windowH)
    {
        SDL_ReleaseGPUTexture(gpuDevice, m_DepthBuffer);
        m_DepthBuffer = nullptr;

        m_DepthCreateInfo.width = windowW;
        m_DepthCreateInfo.height = windowH;

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

    adCmdBuf.Cancel();

    auto cleanup = AutoDeleter([](auto r, auto c)
    {
        SDL_EndGPURenderPass(r); SDL_SubmitGPUCommandBuffer(c);
    }, renderPass, cmdBuf);

    SDL_GPUViewport viewport
    {
        0, 0, (float)windowW, (float)windowH, 0, 1
    };
    SDL_SetGPUViewport(renderPass, &viewport);

    for (auto& [mtlId, xmeshes] : m_MeshGroups)
    {
        auto mtlResult = m_GpuDevice->GetMaterial(mtlId);
        if (!mtlResult)
        {
            logError(mtlResult.error().Message);
            continue;
        }

        auto mtl = mtlResult.value();

        SDL_PushGPUVertexUniformData(cmdBuf, 1, &mtl->Color, sizeof(mtl->Color));

        //const int idx = m_MaterialDb->GetIndex(mtlId);

        //SDL_PushGPUVertexUniformData(m_CmdBuf, 2, &idx, sizeof(idx));
        const int idx = 0;
        SDL_PushGPUVertexUniformData(cmdBuf, 2, &idx, sizeof(idx));

        if (mtl->Albedo)
        {
            if (!everify(mtl->AlbedoSampler))
            {
                continue;
            }

            // Bind texture and sampler
            SDL_GPUTextureSamplerBinding samplerBinding
            {
                .texture = mtl->Albedo,
                .sampler = mtl->AlbedoSampler
            };
            SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);
        }

        auto pipelineResult = m_GpuDevice->GetOrCreatePipeline(*mtl);

        expect(pipelineResult, pipelineResult.error());

        SDL_BindGPUGraphicsPipeline(renderPass, pipelineResult.value());

        for (auto& xmesh : xmeshes)
        {
            const Mat44 xform = camera.ViewProj().Mul(m_Transforms[xmesh.TransformIdx]);

            SDL_PushGPUVertexUniformData(cmdBuf, 0, &xform, sizeof(xform));

            SDL_GPUBufferBinding vertexBufferBinding
            {
                .buffer = xmesh.Mesh->VtxBuffer.Get<SDLVertexBuffer>()->m_Buffer,
                .offset = 0
            };
            SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);

            SDL_GPUBufferBinding indexBufferBinding
            {
                .buffer = xmesh.Mesh->IdxBuffer.Get<SDLIndexBuffer>()->m_Buffer,
                .offset = 0
            };

            static_assert(VERTEX_INDEX_BITS == 32 || VERTEX_INDEX_BITS == 16);

            const SDL_GPUIndexElementSize idxElSize =
                (VERTEX_INDEX_BITS == 32)
                ? SDL_GPU_INDEXELEMENTSIZE_32BIT
                : SDL_GPU_INDEXELEMENTSIZE_16BIT;

            SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, idxElSize);

            SDL_DrawGPUIndexedPrimitives(renderPass, xmesh.Mesh->IndexCount, 1, xmesh.Mesh->IndexOffset, 0, 0);
        }
    }

    SDL_EndGPURenderPass(renderPass);

    cleanup.Cancel();

    auto fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdBuf);

    expect(fence, SDL_GetError());

    expect(SDL_WaitForGPUFences(gpuDevice, true, &fence, 1), SDL_GetError())

    SDL_ReleaseGPUFence(gpuDevice, fence);

    return {};
}

void
SDLRenderGraph::Reset()
{
    m_Transforms.clear();
    for (auto& [mtlId, meshes] : m_MeshGroups)
    {
        meshes.clear();
    }
}