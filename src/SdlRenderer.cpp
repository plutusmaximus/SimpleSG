#define _CRT_SECURE_NO_WARNINGS

#define __LOGGER_NAME__ "SDL "

#include "SdlRenderer.h"

#include "Logging.h"

#include "Result.h"
#include "scope_exit.h"

#include "SdlGpuDevice.h"

#include <SDL3/SDL_gpu.h>

#include <cstdio>

SdlRenderer::SdlRenderer(SdlGpuDevice* gpuDevice, GpuPipeline* pipeline)
    : m_GpuDevice(gpuDevice)
    , m_Pipeline(pipeline)
{
}

SdlRenderer::~SdlRenderer()
{
    WaitForFence();

    if(m_DefaultBaseTexture)
    {
        auto result = m_GpuDevice->DestroyTexture(m_DefaultBaseTexture);
        if(!result)
        {
            logError("Failed to destroy default base texture: {}", result.error());
        }
    }

    if(m_ColorTarget)
    {
        auto result = m_GpuDevice->DestroyColorTarget(m_ColorTarget);
        if(!result)
        {
            logError("Failed to destroy default color target: {}", result.error());
        }
    }

    if(m_DepthTarget)
    {
        auto result = m_GpuDevice->DestroyDepthTarget(m_DepthTarget);
        if(!result)
        {
            logError("Failed to destroy default depth target: {}", result.error());
        }
    }

    if(m_CopyTextureVertexShader)
    {
        auto result = m_GpuDevice->DestroyVertexShader(m_CopyTextureVertexShader);
        if(!result)
        {
            logError("Failed to destroy copy texture vertex shader: {}", result.error());
        }
    }

    if(m_CopyTextureFragmentShader)
    {
        auto result = m_GpuDevice->DestroyFragmentShader(m_CopyTextureFragmentShader);
        if(!result)
        {
            logError("Failed to destroy copy texture fragment shader: {}", result.error());
        }
    }

    if(m_CopyTexturePipeline)
    {
        SDL_ReleaseGPUGraphicsPipeline(m_GpuDevice->Device, m_CopyTexturePipeline);
    }

    for(const auto& state: m_State)
    {
        eassert(!state.m_RenderFence, "Render fence must be null when destroying SdlRenderer");
    }
}

void
SdlRenderer::Add(const Mat44f& worldTransform, const Model* model)
{
    if(!everify(model, "Model pointer is null"))
    {
        return;
    }

    const auto& meshes = model->GetMeshes();
    const auto& meshInstances = model->GetMeshInstances();
    const auto& transformNodes = model->GetTransformNodes();

    std::vector<Mat44f> worldXForms;
    worldXForms.reserve(transformNodes.size());

    // Precompute world transforms for all nodes
    for(const auto& node : transformNodes)
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

    for (const auto& meshInstance : meshInstances)
    {
        const Mesh& mesh = meshes[meshInstance.MeshIndex];
        const Material& mtl = mesh.GetMaterial();

        // Determine mesh group based on material properties

        MeshGroup* meshGrp;

        const MaterialKey& key = mtl.GetKey();

        if(key.Flags & MaterialFlags::Translucent)
        {
            meshGrp = &m_CurrentState->m_TranslucentMeshGroups[key.Id];
        }
        else
        {
            meshGrp = &m_CurrentState->m_OpaqueMeshGroups[key.Id];
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
SdlRenderer::Render(const Mat44f& camera, const Mat44f& projection)
{
    //Wait for the previous frame to complete
    WaitForFence();

    auto gpuDevice = m_GpuDevice->Device;

    SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(gpuDevice);
    expect(cmdBuf, SDL_GetError());

    auto renderPassResult = BeginRenderPass(cmdBuf);

    if(!renderPassResult)
    {
        SDL_CancelGPUCommandBuffer(cmdBuf);
        return Error(renderPassResult.error());
    }

    auto renderPass = renderPassResult.value();

    if(!renderPass)
    {
        //No render pass - likely window minimized.
        //This is not an error.
        SDL_CancelGPUCommandBuffer(cmdBuf);
        return Result<void>::Success;
    }

    auto cleanupRenderPass = scope_exit([&]()
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
            const Material& mtl = xmeshes[0].MeshInstance.GetMaterial();
            //const int mtlIdx = m_MaterialDb->GetIndex(mtlId);

            GpuTexture* baseTexture = mtl.GetBaseTexture();

            if(!baseTexture)
            {
                // If material doesn't have a base texture, bind a default texture.
                auto defaultTextResult = GetDefaultBaseTexture();
                expect(defaultTextResult, defaultTextResult.error());

                baseTexture = defaultTextResult.value();
            }

            // Bind texture and sampler
            SDL_GPUTextureSamplerBinding samplerBinding
            {
                .texture = static_cast<SdlGpuTexture*>(baseTexture)->GetTexture(),
                .sampler = static_cast<SdlGpuTexture*>(baseTexture)->GetSampler()
            };

            auto pipeline = static_cast<SdlGpuPipeline*>(m_Pipeline)->GetPipeline();

            SDL_PushGPUVertexUniformData(cmdBuf, 1, &mtl.GetColor(), sizeof(mtl.GetColor()));
            SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);
            SDL_BindGPUGraphicsPipeline(renderPass, pipeline);

            const Mat44f viewProj = projection.Mul(viewXform);

            for (auto& xmesh : xmeshes)
            {
                const Mat44f matrices[] =
                {
                    xmesh.WorldTransform,
                    viewProj.Mul(xmesh.WorldTransform)
                };

                const Mesh& mesh = xmesh.MeshInstance;

                const SdlGpuVertexBuffer* sdlVb =
                    static_cast<const SdlGpuVertexBuffer*>(mesh.GetVertexBuffer().GetBuffer());

                SDL_GPUBufferBinding vertexBufferBinding
                {
                    .buffer = sdlVb->GetBuffer(),
                    .offset = mesh.GetVertexBuffer().GetByteOffset()
                };

                const SdlGpuIndexBuffer* sdlIb =
                    static_cast<const SdlGpuIndexBuffer*>(mesh.GetIndexBuffer().GetBuffer());

                SDL_GPUBufferBinding indexBufferBinding
                {
                    .buffer = sdlIb->GetBuffer(),
                    .offset = mesh.GetIndexBuffer().GetByteOffset()
                };

                static_assert(VERTEX_INDEX_BITS == 32 || VERTEX_INDEX_BITS == 16);

                constexpr SDL_GPUIndexElementSize idxElSize =
                    (VERTEX_INDEX_BITS == 32)
                    ? SDL_GPU_INDEXELEMENTSIZE_32BIT
                    : SDL_GPU_INDEXELEMENTSIZE_16BIT;

                // Send up the model and model-view-projection matrices
                SDL_PushGPUVertexUniformData(cmdBuf, 0, matrices, sizeof(matrices));
                SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);
                SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, idxElSize);
                SDL_DrawGPUIndexedPrimitives(renderPass, mesh.GetIndexCount(), 1, 0, 0, 0);
            }
        }
    }

    SDL_EndGPURenderPass(renderPass);

    cleanupRenderPass.release();

    auto copyResult = CopyColorTargetToSwapchain(cmdBuf);
    expect(copyResult, copyResult.error());

    SwapStates();

    eassert(!m_CurrentState->m_RenderFence, "Render fence should be null here");

    m_CurrentState->m_RenderFence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdBuf);
    expect(m_CurrentState->m_RenderFence, SDL_GetError());

    return Result<void>::Success;
}

//private:

Result<SDL_GPURenderPass*>
SdlRenderer::BeginRenderPass(SDL_GPUCommandBuffer* cmdBuf)
{
    const auto screenBounds = m_GpuDevice->GetScreenBounds();

    const unsigned targetWidth = static_cast<unsigned>(screenBounds.Width);
    const unsigned targetHeight = static_cast<unsigned>(screenBounds.Height);

    if(!m_ColorTarget || m_ColorTarget->GetWidth() != targetWidth ||
        m_ColorTarget->GetHeight() != targetHeight)
    {
        if(m_ColorTarget)
        {
            auto result = m_GpuDevice->DestroyColorTarget(m_ColorTarget);
            if(!result)
            {
                logError("Failed to destroy default color target: {}", result.error());
            }
            m_ColorTarget = nullptr;
        }

        auto result = m_GpuDevice->CreateColorTarget(targetWidth, targetHeight, "ColorTarget");
        expect(result, result.error());
        m_ColorTarget = result.value();
    }

    if(!m_DepthTarget || m_DepthTarget->GetWidth() != targetWidth ||
        m_DepthTarget->GetHeight() != targetHeight)
    {
        if(m_DepthTarget)
        {
            auto result = m_GpuDevice->DestroyDepthTarget(m_DepthTarget);
            if(!result)
            {
                logError("Failed to destroy default depth target: {}", result.error());
            }
            m_DepthTarget = nullptr;
        }

        auto result = m_GpuDevice->CreateDepthTarget(targetWidth, targetHeight, "DepthTarget");
        expect(result, result.error());
        m_DepthTarget = result.value();
    }

    SDL_GPUColorTargetInfo colorTargetInfo
    {
        .texture = static_cast<SdlGpuColorTarget*>(m_ColorTarget)->GetColorTarget(),
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = {0, 0, 0, 0},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE
    };

    static constexpr float CLEAR_DEPTH = 1.0f;

    SDL_GPUDepthStencilTargetInfo depthTargetInfo
    {
        .texture = static_cast<SdlGpuDepthTarget*>(m_DepthTarget)->GetDepthTarget(),
        .clear_depth = CLEAR_DEPTH,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE
    };

    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(
        cmdBuf,
        &colorTargetInfo,
        1,
        &depthTargetInfo);

    if(!renderPass)
    {
        // If we fail to begin the render pass, it's likely because the window is minimized and the
        // swapchain texture is not available. In this case, we can just skip rendering this frame
        // without treating it as an error.
        return nullptr;
    }

    const SDL_GPUViewport viewport
    {
        0, 0, static_cast<float>(targetWidth), static_cast<float>(targetHeight), 0, CLEAR_DEPTH
    };
    SDL_SetGPUViewport(renderPass, &viewport);

    return renderPass;
}

void
SdlRenderer::WaitForFence()
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
        logError("Error waiting for render fence during SdlRenderer destruction: {}", SDL_GetError());
    }

    SDL_ReleaseGPUFence(m_GpuDevice->Device, m_CurrentState->m_RenderFence);
    m_CurrentState->m_RenderFence = nullptr;
}

void
SdlRenderer::SwapStates()
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

Result<void>
SdlRenderer::CopyColorTargetToSwapchain(SDL_GPUCommandBuffer* cmdBuf)
{
    SDL_GPUTexture* swap;
    unsigned swapW, swapH;
    expect(
        SDL_WaitAndAcquireGPUSwapchainTexture(cmdBuf, m_GpuDevice->Window, &swap, &swapW, &swapH),
        SDL_GetError());

    auto pipelineResult = GetCopyColorTargetPipeline();
    expect(pipelineResult, pipelineResult.error());

    auto pipeline = pipelineResult.value();

    SDL_GPUColorTargetInfo colorTargetInfo
    {
        .texture = swap,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = {0, 0, 0, 0},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE
    };

    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(
        cmdBuf,
        &colorTargetInfo,
        1,
        nullptr);

    if(!renderPass)
    {
        // If we fail to begin the render pass, it's likely because the window is minimized and the
        // swapchain texture is not available. In this case, we can just skip rendering this frame
        // without treating it as an error.
        return Result<void>::Success;
    }

    const SDL_GPUViewport viewport//
    {
        0, 0, static_cast<float>(swapW), static_cast<float>(swapH), 0, 1.0f,
    };
    SDL_SetGPUViewport(renderPass, &viewport);

    // Bind texture and sampler
    SDL_GPUTextureSamplerBinding samplerBinding
    {
        .texture = static_cast<SdlGpuColorTarget*>(m_ColorTarget)->GetColorTarget(),
        .sampler = static_cast<SdlGpuColorTarget*>(m_ColorTarget)->GetSampler()
    };

    SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);
    SDL_BindGPUGraphicsPipeline(renderPass, pipeline);
    SDL_DrawGPUPrimitives(renderPass, 3, 1, 0, 1);
    SDL_EndGPURenderPass(renderPass);

    return Result<void>::Success;
}

Result<GpuTexture*>
SdlRenderer::GetDefaultBaseTexture()
{
    if(!m_DefaultBaseTexture)
    {
        static constexpr const char* MAGENTA_TEXTURE_KEY = "$magenta";

        auto result = m_GpuDevice->CreateTexture("#FF00FFFF"_rgba, imstring(MAGENTA_TEXTURE_KEY));
        expect(result, result.error());

        m_DefaultBaseTexture = result.value();
    }

    return m_DefaultBaseTexture;
}

static Result<void>
LoadShaderCode(const char* filePath, std::vector<uint8_t>& outBuffer)
{
    FILE* fp = std::fopen(filePath, "rb");
    expect(fp, "Failed to open shader file: {} ({})", filePath, std::strerror(errno));

    auto cleanupFile = scope_exit([&]() { std::fclose(fp); });

    //Get file size
    if(std::fseek(fp, 0, SEEK_END) != 0)
    {
        return Error("Failed to seek in shader file: {} ({})", filePath, std::strerror(errno));
    }

    long fileSize = std::ftell(fp);
    if(fileSize < 0)
    {
        return Error("Failed to get size of shader file: {} ({})", filePath, std::strerror(errno));
    }
    std::rewind(fp);

    outBuffer.resize(static_cast<size_t>(fileSize));

    expect(std::fread(outBuffer.data(), 1, static_cast<size_t>(fileSize), fp) ==
                static_cast<size_t>(fileSize),
            "Failed to read shader file: {} ({})", filePath, std::strerror(errno));

    return Result<void>::Success;
}

Result<GpuVertexShader*>
SdlRenderer::GetCopyColorTargetVertexShader()
{
    if(m_CopyTextureVertexShader)
    {
        return m_CopyTextureVertexShader;
    }

    std::vector<uint8_t> shaderCode;
    auto loadResult = LoadShaderCode("shaders/Debug/FullScreenTriangle.vs.spv", shaderCode);
    expect(loadResult, loadResult.error());

    std::span<uint8_t> shaderCodeSpan(shaderCode.data(), shaderCode.size());

    auto vsResult = m_GpuDevice->CreateVertexShader(shaderCodeSpan);
    expect(vsResult, vsResult.error());

    m_CopyTextureVertexShader = vsResult.value();
    return m_CopyTextureVertexShader;
}

Result<GpuFragmentShader*>
SdlRenderer::GetCopyColorTargetFragmentShader()
{
    if(m_CopyTextureFragmentShader)
    {
        return m_CopyTextureFragmentShader;
    }

    std::vector<uint8_t> shaderCode;
    auto loadResult = LoadShaderCode("shaders/Debug/FullScreenTriangle.ps.spv", shaderCode);
    expect(loadResult, loadResult.error());

    std::span<uint8_t> shaderCodeSpan(shaderCode.data(), shaderCode.size());

    auto fsResult = m_GpuDevice->CreateFragmentShader(shaderCodeSpan);
    expect(fsResult, fsResult.error());

    m_CopyTextureFragmentShader = fsResult.value();
    return m_CopyTextureFragmentShader;
}

Result<SDL_GPUGraphicsPipeline*>
SdlRenderer::GetCopyColorTargetPipeline()
{
    if(m_CopyTexturePipeline)
    {
        return m_CopyTexturePipeline;
    }

    auto vsResult = GetCopyColorTargetVertexShader();
    expect(vsResult, vsResult.error());

    auto fsResult = GetCopyColorTargetFragmentShader();
    expect(fsResult, fsResult.error());

    auto vs = static_cast<SdlGpuVertexShader*>(vsResult.value());
    auto fs = static_cast<SdlGpuFragmentShader*>(fsResult.value());

    auto colorTargetFormat = SDL_GetGPUSwapchainTextureFormat(m_GpuDevice->Device, m_GpuDevice->Window);

    SDL_GPUColorTargetDescription colorTargetDesc//
    {
        .format = colorTargetFormat,
        .blend_state =
        {
            .enable_blend = false,
            .enable_color_write_mask = false,
        },
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo //
        {
            .vertex_shader = vs->GetShader(),
            .fragment_shader = fs->GetShader(),
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = //
            {
                .fill_mode = SDL_GPU_FILLMODE_FILL,
                .cull_mode = SDL_GPU_CULLMODE_BACK,
                .front_face = SDL_GPU_FRONTFACE_CLOCKWISE,
                .enable_depth_clip = false,
            },
            .depth_stencil_state = //
            {
                .enable_depth_test = false,
                .enable_depth_write = false,
            },
            .target_info = //
            {
                .color_target_descriptions = &colorTargetDesc,
                .num_color_targets = 1,
                .has_depth_stencil_target = false,
            },
        };

    m_CopyTexturePipeline = SDL_CreateGPUGraphicsPipeline(m_GpuDevice->Device, &pipelineCreateInfo);
    expect(m_CopyTexturePipeline, SDL_GetError());

    return m_CopyTexturePipeline;
}