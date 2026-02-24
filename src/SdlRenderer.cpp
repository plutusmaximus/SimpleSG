#define _CRT_SECURE_NO_WARNINGS

#define __LOGGER_NAME__ "SDL "

#include "SdlRenderer.h"

#include "Logging.h"

#include "Result.h"
#include "scope_exit.h"

#include "SdlGpuDevice.h"
#include "PerfMetrics.h"

#include <SDL3/SDL_gpu.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>

#include <cstdio>

static constexpr const char* COMPOSITE_COLOR_TARGET_VS = "shaders/Debug/FullScreenTriangle.vs.spv";
static constexpr const char* COMPOSITE_COLOR_TARGET_FS = "shaders/Debug/FullScreenTriangle.ps.spv";

static constexpr const char* COLOR_PIPELINE_VS = "shaders/Debug/VertexShader.vs.spv";
static constexpr const char* COLOR_PIPELINE_FS = "shaders/Debug/FragmentShader.ps.spv";

SdlRenderer::SdlRenderer(SdlGpuDevice* gpuDevice)
    : m_GpuDevice(gpuDevice)
{
    InitGui();
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

    if(m_ColorVertexShader)
    {
        auto result = m_GpuDevice->DestroyVertexShader(m_ColorVertexShader);
        if(!result)
        {
            logError("Failed to destroy color vertex shader: {}", result.error());
        }
    }

    if(m_ColorFragmentShader)
    {
        auto result = m_GpuDevice->DestroyFragmentShader(m_ColorFragmentShader);
        if(!result)
        {
            logError("Failed to destroy color fragment shader: {}", result.error());
        }
    }

    if(m_ColorPipeline)
    {
        SDL_ReleaseGPUGraphicsPipeline(m_GpuDevice->Device, m_ColorPipeline);
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

#ifndef NDEBUG
    for(const auto& state: m_State)
    {
        eassert(!state.m_RenderFence, "Render fence must be null when destroying SdlRenderer");
    }
#endif  // NDEBUG

    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(m_ImGuiContext);
}

Result<void>
SdlRenderer::NewFrame()
{
    if(!everify(m_NewFrameCount == m_RenderCount))
    {
        return Result<void>::Success;
    }

    ++m_NewFrameCount;

    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    return Result<void>::Success;
}

void
SdlRenderer::AddModel(const Mat44f& worldTransform, const Model* model)
{
    if(!everify(model, "Model pointer is null"))
    {
        return;
    }

    if(!everify(m_RenderCount == m_NewFrameCount - 1))
    {
        // Forgot to call NewFrame()
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
    static PerfTimer renderTimer("Renderer.Render");
    auto scopedRenderTimer = renderTimer.StartScoped();

    if(!everify(m_RenderCount == m_NewFrameCount - 1))
    {
        return Error("Render called without a matching NewFrame");
    }

    ++m_RenderCount;

    //Wait for the previous frame to complete
    static PerfTimer waitForFenceTimer("Renderer.Render.WaitForFence");
    {
        auto scopedWaitForFenceTimer = waitForFenceTimer.StartScoped();
        WaitForFence();
    }

    auto gpuDevice = m_GpuDevice->Device;

    static PerfTimer acquireCmdBufTimer("Renderer.Render.AcquireCommandBuffer");
    SDL_GPUCommandBuffer* cmdBuf;
    {
        auto scopedAcquireCmdBufTimer = acquireCmdBufTimer.StartScoped();
        cmdBuf = SDL_AcquireGPUCommandBuffer(gpuDevice);
        expect(cmdBuf, SDL_GetError());
    }

    SDL_GPUTexture* swapchainTexture = nullptr;

#if !OFFSCREEN_RENDERING
    expect(
        SDL_WaitAndAcquireGPUSwapchainTexture(cmdBuf, m_GpuDevice->Window, &swapchainTexture, nullptr, nullptr),
        SDL_GetError());

    if(!swapchainTexture)
    {
        // No swapchain texture - likely window is minimized.
        // This is not an error, just skip rendering.
         SDL_CancelGPUCommandBuffer(cmdBuf);
         return Result<void>::Success;
    }
#endif

    SDL_GPURenderPass* renderPass = nullptr;
    static PerfTimer beginRenderPassTimer("Renderer.Render.BeginRenderPass");
    {
        auto scopedBeginRenderPassTimer = beginRenderPassTimer.StartScoped();
        auto renderPassResult = BeginRenderPass(cmdBuf);

        if(!renderPassResult)
        {
            SDL_CancelGPUCommandBuffer(cmdBuf);
            return Error(renderPassResult.error());
        }

        renderPass = renderPassResult.value();
    }

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

    static PerfTimer drawTimer("Renderer.Render.Draw");
    drawTimer.Start();

    static PerfTimer setPipelineTimer("Renderer.Render.SetPipeline");
    {
        auto scopedTimer = setPipelineTimer.StartScoped();
        auto pipelineResult = GetColorPipeline();
        expect(pipelineResult, pipelineResult.error());
        auto pipeline = pipelineResult.value();
        SDL_BindGPUGraphicsPipeline(renderPass, pipeline);
    }

    for(const auto meshGrpPtr : meshGroups)
    {
        for (auto& [mtlId, xmeshes] : *meshGrpPtr)
        {
            const Material& mtl = xmeshes[0].MeshInstance.GetMaterial();
            //const int mtlIdx = m_MaterialDb->GetIndex(mtlId);

            /*GpuTexture* baseTexture = mtl.GetBaseTexture();

            if(!baseTexture)
            {
                // If material doesn't have a base texture, bind a default texture.
                auto defaultTextResult = GetDefaultBaseTexture();
                expect(defaultTextResult, defaultTextResult.error());

                baseTexture = defaultTextResult.value();
            }*/

            static PerfTimer writeMaterialTimer("Renderer.Render.Draw.WriteMaterialBuffer");
            {
                auto scopedTimer = writeMaterialTimer.StartScoped();

                SDL_PushGPUFragmentUniformData(cmdBuf, 0, &mtl.GetConstants(), sizeof(MaterialConstants));
            }

            // Bind texture and sampler

            GpuTexture* baseTexture = mtl.GetBaseTexture();

            SDL_GPUTextureSamplerBinding samplerBinding
            {
                .texture = static_cast<SdlGpuTexture*>(baseTexture)->GetTexture(),
                .sampler = static_cast<SdlGpuTexture*>(baseTexture)->GetSampler()
            };
            static PerfTimer fsBindingTimer("Renderer.Render.Draw.FsBindings");
            {
                auto scopedTimer = fsBindingTimer.StartScoped();
                SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);
            }

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

                static PerfTimer setBuffersTimer("Renderer.Render.Draw.SetBuffers");
                {
                    auto scopedTimer = setBuffersTimer.StartScoped();
                    SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);
                    SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, idxElSize);
                }

                static PerfTimer writeTransformTimer("Renderer.Render.Draw.WriteTransformBuffer");
                {
                    auto scopedTimer = writeTransformTimer.StartScoped();
                    // Send up the model and model-view-projection matrices
                    SDL_PushGPUVertexUniformData(cmdBuf, 0, matrices, sizeof(matrices));
                }

                static PerfTimer drawIndexedTimer("Renderer.Render.Draw.DrawIndexed");
                {
                    auto scopedTimer = drawIndexedTimer.StartScoped();
                    SDL_DrawGPUIndexedPrimitives(renderPass, mesh.GetIndexCount(), 1, 0, 0, 0);
                }
            }
        }
    }

    drawTimer.Stop();

    SDL_EndGPURenderPass(renderPass);

    cleanupRenderPass.release();

    static PerfTimer resolveTimer("Renderer.Render.Resolve");
    resolveTimer.Start();

    static PerfTimer copyTimer("Renderer.Render.Resolve.CopyColorTarget");
    {
        auto scopedTimer = copyTimer.StartScoped();
        auto copyResult = CopyColorTargetToSwapchain(cmdBuf, swapchainTexture);
        expect(copyResult, copyResult.error());
    }

    static PerfTimer renderGuiTimer("Renderer.Render.Resolve.RenderGUI");
    {
        auto scopedTimer = renderGuiTimer.StartScoped();
        auto renderGuiResult = RenderGui(cmdBuf, swapchainTexture);
        expect(renderGuiResult, renderGuiResult.error());
    }

    SwapStates();

    eassert(!m_CurrentState->m_RenderFence, "Render fence should be null here");

    static PerfTimer submitCmdBufferTimer("Renderer.Render.Resolve.SubmitCommandBuffer");
    {
        auto scopedTimer = submitCmdBufferTimer.StartScoped();
        //expect(SDL_SubmitGPUCommandBuffer(cmdBuf), SDL_GetError());
        m_CurrentState->m_RenderFence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdBuf);
        expect(m_CurrentState->m_RenderFence, SDL_GetError());
    }

    resolveTimer.Stop();

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
        logDebug("Creating new color target for render pass with size {}x{}", targetWidth, targetHeight);

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
        logDebug("Creating new depth target for render pass with size {}x{}", targetWidth, targetHeight);

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

    /*const SDL_GPUViewport viewport
    {
        0, 0, static_cast<float>(targetWidth), static_cast<float>(targetHeight), 0, CLEAR_DEPTH
    };
    SDL_SetGPUViewport(renderPass, &viewport);*/

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
SdlRenderer::CopyColorTargetToSwapchain(SDL_GPUCommandBuffer* cmdBuf, SDL_GPUTexture* target)
{
    if(!target)
    {
        // Offscreen rendering - no swapchain texture available. Not an error, just skip copying.
        return Result<void>::Success;
    }

    auto pipelineResult = GetCopyColorTargetPipeline();
    expect(pipelineResult, pipelineResult.error());

    auto pipeline = pipelineResult.value();

    SDL_GPUColorTargetInfo colorTargetInfo
    {
        .texture = target,
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

Result<GpuVertexShader*> SdlRenderer::GetColorVertexShader()
{
    if(!m_ColorVertexShader)
    {
        auto vsResult = CreateVertexShader(COLOR_PIPELINE_VS);
        expect(vsResult, vsResult.error());

        m_ColorVertexShader = vsResult.value();
    }

    return m_ColorVertexShader;
}

Result<GpuFragmentShader*> SdlRenderer::GetColorFragmentShader()
{
    if(!m_ColorFragmentShader)
    {
        auto fsResult = CreateFragmentShader(COLOR_PIPELINE_FS);
        expect(fsResult, fsResult.error());

        m_ColorFragmentShader = fsResult.value();
    }

    return m_ColorFragmentShader;
}

Result<SDL_GPUGraphicsPipeline*>
SdlRenderer::GetColorPipeline()
{
    if(m_ColorPipeline)
    {
        return m_ColorPipeline;
    }

    if(!everify(m_ColorTarget, "Color target is null"))
    {
        return Error("Color target is null");
    }

    auto vertexShaderResult = GetColorVertexShader();
    expect(vertexShaderResult, vertexShaderResult.error());
    auto vertexShader = vertexShaderResult.value();

    auto fragmentShaderResult = GetColorFragmentShader();
    expect(fragmentShaderResult, fragmentShaderResult.error());
    auto fragmentShader = fragmentShaderResult.value();

    SDL_GPUVertexBufferDescription vertexBufDescriptions[1] = //
        {
            {
                .slot = 0,
                .pitch = sizeof(Vertex),
                .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            },
        };
    SDL_GPUVertexAttribute vertexAttributes[] = //
        {
            {
                //
                .location = 0,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                .offset = offsetof(Vertex, pos),
            },
            {
                //
                .location = 1,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                .offset = offsetof(Vertex, normal),
            },
            { //
                .location = 2,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                .offset = offsetof(Vertex, uvs[0]),
            },
        };

    SDL_GPUColorTargetDescription colorTargetDesc//
    {
        .format = static_cast<SdlGpuColorTarget*>(m_ColorTarget)->GetFormat(),
        .blend_state =
        {
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .color_write_mask = SDL_GPU_COLORCOMPONENT_R |
                               SDL_GPU_COLORCOMPONENT_G |
                               SDL_GPU_COLORCOMPONENT_B |
                               SDL_GPU_COLORCOMPONENT_A,
            .enable_blend = true,
            .enable_color_write_mask = false,
        },
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo //
        {
            .vertex_shader = static_cast<SdlGpuVertexShader*>(vertexShader)->GetShader(),
            .fragment_shader = static_cast<SdlGpuFragmentShader*>(fragmentShader)->GetShader(),
            .vertex_input_state = //
            {
                .vertex_buffer_descriptions = vertexBufDescriptions,
                .num_vertex_buffers = 1,
                .vertex_attributes = vertexAttributes,
                .num_vertex_attributes = std::size(vertexAttributes),
            },
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = //
            {
                .fill_mode = SDL_GPU_FILLMODE_FILL,
                .cull_mode = SDL_GPU_CULLMODE_BACK,
                .front_face = SDL_GPU_FRONTFACE_CLOCKWISE,
                .enable_depth_clip = true,
            },
            .depth_stencil_state = //
            {
                .compare_op = SDL_GPU_COMPAREOP_LESS,
                .enable_depth_test = true,
                .enable_depth_write = true,
            },
            .target_info = //
            {
                .color_target_descriptions = &colorTargetDesc,
                .num_color_targets = 1,
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                .has_depth_stencil_target = true,
            },
        };

    m_ColorPipeline = SDL_CreateGPUGraphicsPipeline(m_GpuDevice->Device, &pipelineCreateInfo);
    expect(m_ColorPipeline, SDL_GetError());

    return m_ColorPipeline;
}

Result<GpuVertexShader*>
SdlRenderer::GetCopyColorTargetVertexShader()
{
    if(!m_CopyTextureVertexShader)
    {
        auto vsResult = CreateVertexShader(COMPOSITE_COLOR_TARGET_VS);
        expect(vsResult, vsResult.error());

        m_CopyTextureVertexShader = vsResult.value();
    }

    return m_CopyTextureVertexShader;
}

Result<GpuFragmentShader*>
SdlRenderer::GetCopyColorTargetFragmentShader()
{
    if(!m_CopyTextureFragmentShader)
    {
        auto fsResult = CreateFragmentShader(COMPOSITE_COLOR_TARGET_FS);
        expect(fsResult, fsResult.error());

        m_CopyTextureFragmentShader = fsResult.value();
    }

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

    auto colorTargetFormat = m_GpuDevice->GetSwapChainFormat();

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

Result<GpuVertexShader*>
SdlRenderer::CreateVertexShader(const char* path)
{
    std::vector<uint8_t> shaderCode;
    auto loadResult = LoadShaderCode(path, shaderCode);
    expect(loadResult, loadResult.error());

    std::span<uint8_t> shaderCodeSpan(shaderCode.data(), shaderCode.size());

    return m_GpuDevice->CreateVertexShader(shaderCodeSpan);
}

Result<GpuFragmentShader*>
SdlRenderer::CreateFragmentShader(const char* path)
{
    std::vector<uint8_t> shaderCode;
    auto loadResult = LoadShaderCode(path, shaderCode);
    expect(loadResult, loadResult.error());

    std::span<uint8_t> shaderCodeSpan(shaderCode.data(), shaderCode.size());

    return m_GpuDevice->CreateFragmentShader(shaderCodeSpan);
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

Result<void>
SdlRenderer::InitGui()
{
    if(m_ImGuiContext)
    {
        // Already initialized
        return Result<void>::Success;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    m_ImGuiContext = ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    //io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForSDLGPU(m_GpuDevice->Window);
    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = m_GpuDevice->Device;
    init_info.ColorTargetFormat = m_GpuDevice->GetSwapChainFormat();
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;                      // Only used in multi-viewports mode.
    init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;  // Only used in multi-viewports mode.
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&init_info);

    return Result<void>::Success;
}

Result<void>
SdlRenderer::RenderGui(SDL_GPUCommandBuffer* cmdBuf, SDL_GPUTexture* target)
{
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();

    if(!drawData || drawData->TotalVtxCount == 0)
    {
        // Nothing to render for ImGui
        return Result<void>::Success;
    }

    const bool is_minimized = (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f);

    if(is_minimized || !target)
    {
        // If the window is minimized, we can skip rendering the GUI without treating it as an error.
        return Result<void>::Success;
    }

    if(!target)
    {
        // Offscreen rendering - no swapchain texture available. Not an error, just skip copying.
        return Result<void>::Success;
    }

    // This is mandatory: call ImGui_ImplSDLGPU3_PrepareDrawData() to upload the vertex/index buffer!
    ImGui_ImplSDLGPU3_PrepareDrawData(drawData, cmdBuf);

    // Setup and start a render pass
    SDL_GPUColorTargetInfo target_info//
    {
        .texture = target,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = {0, 0, 0, 0},
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
        .cycle = false,
    };

    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(cmdBuf, &target_info, 1, nullptr);
    expect(renderPass, SDL_GetError());

    // Render ImGui
    ImGui_ImplSDLGPU3_RenderDrawData(drawData, cmdBuf, renderPass);

    SDL_EndGPURenderPass(renderPass);

    return Result<void>::Success;
}