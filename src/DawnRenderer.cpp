#define _CRT_SECURE_NO_WARNINGS

#define __LOGGER_NAME__ "DAWN"

#include "DawnRenderer.h"

#include "Logging.h"

#include "Result.h"
#include "scope_exit.h"

#include "DawnGpuDevice.h"
#include "PerfMetrics.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>

#include <cstdio>

DawnRenderer::DawnRenderer(DawnGpuDevice* gpuDevice, GpuPipeline* pipeline)
    : m_GpuDevice(gpuDevice)
    , m_Pipeline(pipeline)
{
    InitGui();
}

DawnRenderer::~DawnRenderer()
{
    //DO NOT SUBMIT
    //WaitForFence();

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
        //DO NOT SUBMIT
        //SDL_ReleaseGPUGraphicsPipeline(m_GpuDevice->Device, m_CopyTexturePipeline);
    }

    //DO NOT SUBMIT
    /*for(const auto& state: m_State)
    {
        eassert(!state.m_RenderFence, "Render fence must be null when destroying SdlRenderer");
    }*/

    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(m_ImGuiContext);
}

Result<void>
DawnRenderer::BeginFrame()
{
    if(!everify(m_BeginFrameCount == m_RenderCount))
    {
        return Result<void>::Success;
    }

    ++m_BeginFrameCount;

    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    return Result<void>::Success;
}

void
DawnRenderer::AddModel(const Mat44f& worldTransform, const Model* model)
{
    if(!everify(model, "Model pointer is null"))
    {
        return;
    }

    if(!everify(m_RenderCount == m_BeginFrameCount - 1))
    {
        // Forgot to call BeginFrame()
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
        ++m_CurrentState->m_MeshCount;
    }
}

static inline constexpr uint32_t alignup(const uint32_t value, const uint32_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

Result<void>
DawnRenderer::Render(const Mat44f& camera, const Mat44f& projection)
{
    static PerfTimer renderTimer("Renderer.Render");
    auto scopedRenderTimer = renderTimer.StartScoped();

    if(!everify(m_RenderCount == m_BeginFrameCount - 1))
    {
        return Error("Render called without a matching BeginFrame");
    }

    ++m_RenderCount;

    //Wait for the previous frame to complete
    //DO NOT SUBMIT
    //WaitForFence();

    auto gpuDevice = m_GpuDevice->Device;

    wgpu::Limits limits {};
    gpuDevice.GetLimits(&limits);

    const uint32_t bufferAlign = limits.minUniformBufferOffsetAlignment;

    wgpu::CommandEncoderDescriptor encoderDesc = { .label = "MainRenderEncoder" };

    wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder(&encoderDesc);
    expect(cmdEncoder, "Failed to create command encoder");

    wgpu::TextureView swapchainTextureView;
#if !OFFSCREEN_RENDERING
    wgpu::SurfaceTexture backbuffer;
    m_GpuDevice->Surface.GetCurrentTexture(&backbuffer);
    expect(backbuffer.texture, "Failed to get current surface texture for render pass");

    // TODO - handle SuccessSuboptimal, Timeout, Outdated, Lost, Error statuses
    expect(backbuffer.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal,
        std::format("Backbuffer status: {}", (int)backbuffer.status));

    swapchainTextureView = backbuffer.texture.CreateView();
    expect(swapchainTextureView, "Failed to create texture view for swapchain texture");
#endif

    auto renderPassResult = BeginRenderPass(cmdEncoder);
    expect(renderPassResult, renderPassResult.error());

    auto renderPass = renderPassResult.value();

    auto dawnGpuPipeline = static_cast<DawnGpuPipeline*>(m_Pipeline);

    renderPass.SetPipeline(dawnGpuPipeline->GetPipeline());
    renderPass.SetBindGroup(0, nullptr, 0, nullptr);

    // Set to true to indicate that we need to remake the vertex shader bind group with the new
    // buffers.
    bool remakeVsBindGroup = false;

    // Size of the buffer needed to hold the world and projection matrices for all meshes in the
    // current frame.
    constexpr size_t sizeofTransforms = sizeof(Mat44f) * 2; // World and projection matrices
    const size_t sizeofAlignedTransforms = alignup(sizeofTransforms, bufferAlign);
    const size_t sizeofTransformBuffer = sizeofAlignedTransforms * m_CurrentState->m_MeshCount;

    if(!m_WorldAndProjBuf || m_SizeofTransformBuffer < sizeofTransformBuffer)
    {
        // Re-allocate the world and projection buffer.

        m_SizeofTransformBuffer = sizeofTransformBuffer;

        wgpu::BufferDescriptor worldAndProjBufDesc //
        {
            .label = "WorldAndProjection",
            .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            .size = sizeofTransformBuffer,
            .mappedAtCreation = false,
        };

        m_WorldAndProjBuf = gpuDevice.CreateBuffer(&worldAndProjBufDesc);
        expect(m_WorldAndProjBuf, "Failed to create world and projection buffer");

        remakeVsBindGroup = true;
    }

    const size_t numMeshGroups =
        m_CurrentState->m_OpaqueMeshGroups.size() + m_CurrentState->m_TranslucentMeshGroups.size();

    // Size of the buffer needed to hold the material color for all meshes in the current frame.
    constexpr size_t sizeofMaterialColor =
        sizeof(Vec4f); // Assuming material color is the only data and is a vec4
    const size_t sizeofAlignedMaterialColor = alignup(sizeofMaterialColor, bufferAlign);
    const size_t sizeofMaterialColorBuffer = sizeofAlignedMaterialColor * numMeshGroups;

    if(!m_MaterialColorBuf || m_SizeofMaterialColorBuffer < sizeofMaterialColorBuffer)
    {
        // Re-allocate the material color buffer.

        m_SizeofMaterialColorBuffer = sizeofMaterialColorBuffer;

        wgpu::BufferDescriptor materialColorBufDesc //
        {
            .label = "MaterialColor",
            .usage = wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            .size = sizeofMaterialColorBuffer,
            .mappedAtCreation = false,
        };

        m_MaterialColorBuf = gpuDevice.CreateBuffer(&materialColorBufDesc);
        expect(m_MaterialColorBuf, "Failed to create material color buffer");

        remakeVsBindGroup = true;
    }

    if(remakeVsBindGroup)
    {
        wgpu::BindGroupEntry vsBgEntries[] = //
            {
                {
                    .binding = 0,
                    .buffer = m_WorldAndProjBuf,
                    .offset = 0,
                    .size = sizeofAlignedTransforms,
                },
                {
                    .binding = 1,
                    .buffer = m_MaterialColorBuf,
                    .offset = 0,
                    .size = sizeofAlignedMaterialColor,
                }
            };

        wgpu::BindGroupDescriptor vsBgDesc //
            {
                .label = "vsBindGroup",
                .layout = dawnGpuPipeline->GetVertexBindGroupLayout(),
                .entryCount = std::size(vsBgEntries),
                .entries = vsBgEntries,
            };

        m_VertexShaderBindGroup = gpuDevice.CreateBindGroup(&vsBgDesc);
        expect(m_VertexShaderBindGroup, "Failed to create WGPUBindGroup");
    }

    //DO NOT SUBMIT
    /*if(!renderPass)
    {
        //No render pass - likely window minimized.
        //This is not an error.
        return Result<void>::Success;
    }*/

    //DO NOT SUBMIT
    /*auto cleanupRenderPass = scope_exit([&]()
    {
        SDL_EndGPURenderPass(renderPass);
        SDL_SubmitGPUCommandBuffer(cmdBuf);
    });*/

    // Use inverse of camera transform as view matrix
    const Mat44f viewXform = camera.Inverse();

    // Render opaque meshes first
    const MeshGroupCollection* meshGroups[] =
    {
        &m_CurrentState->m_OpaqueMeshGroups,
        &m_CurrentState->m_TranslucentMeshGroups
    };

    int mtlCount = 0;
    int meshCount = 0;

    static PerfTimer drawTimer("Renderer.Draw");
    drawTimer.Start();

    for(const auto meshGrpPtr : meshGroups)
    {
        for (auto& [mtlId, xmeshes] : *meshGrpPtr)
        {
            const Material& mtl = xmeshes[0].MeshInstance.GetMaterial();

            auto itFragShaderBG = m_FragShaderBindGroups.find(mtlId);
            if(itFragShaderBG == m_FragShaderBindGroups.end())
            {
                // Material bind group doesn't exist yet, create it.

                GpuTexture* baseTexture = mtl.GetBaseTexture();

                if(!baseTexture)
                {
                    // If material doesn't have a base texture, bind a default texture.
                    auto defaultTextResult = GetDefaultBaseTexture();
                    expect(defaultTextResult, defaultTextResult.error());

                    baseTexture = defaultTextResult.value();
                }

                wgpu::BindGroupEntry fsBgEntries[] = //
                    {
                        {
                            .binding = 0,
                            .textureView = static_cast<DawnGpuTexture*>(baseTexture)->GetTextureView(),
                        },
                        {
                            .binding = 1,
                            .sampler = static_cast<DawnGpuTexture*>(baseTexture)->GetSampler(),
                        },
                    };

                wgpu::BindGroupDescriptor fsBgDesc //
                    {
                        .label = "fsBindGroup",
                        .layout = dawnGpuPipeline->GetFragmentBindGroupLayout(),
                        .entryCount = std::size(fsBgEntries),
                        .entries = fsBgEntries,
                    };

                wgpu::BindGroup fsBindGroup = gpuDevice.CreateBindGroup(&fsBgDesc);
                expect(fsBindGroup, "Failed to create WGPUBindGroup");

                itFragShaderBG = m_FragShaderBindGroups.try_emplace(mtlId, fsBindGroup).first;
            }

            renderPass.SetBindGroup(2, itFragShaderBG->second, 0, nullptr);

            gpuDevice.GetQueue().WriteBuffer(m_MaterialColorBuf,
                sizeofAlignedMaterialColor * mtlCount,
                &mtl.GetColor(),
                sizeof(mtl.GetColor()));

            const Mat44f viewProj = projection.Mul(viewXform);

            for (auto& xmesh : xmeshes)
            {
                const Mat44f matrices[] =
                {
                    xmesh.WorldTransform,
                    viewProj.Mul(xmesh.WorldTransform)
                };

                const Mesh& mesh = xmesh.MeshInstance;

                const auto& vbSubrange = mesh.GetVertexBuffer();
                const auto& ibSubrange = mesh.GetIndexBuffer();

                static_assert(VERTEX_INDEX_BITS == 32 || VERTEX_INDEX_BITS == 16);

                constexpr wgpu::IndexFormat idxFmt =
                    (VERTEX_INDEX_BITS == 32)
                    ? wgpu::IndexFormat::Uint32
                    : wgpu::IndexFormat::Uint16;

                constexpr unsigned idxSize = (VERTEX_INDEX_BITS == 32) ? sizeof(uint32_t) : sizeof(uint16_t);

                renderPass.SetVertexBuffer(0,
                    static_cast<const DawnGpuVertexBuffer*>(vbSubrange.GetBuffer())->GetBuffer(),
                    vbSubrange.GetByteOffset(),
                    vbSubrange.GetItemCount() * sizeof(Vertex));

                renderPass.SetIndexBuffer(
                    static_cast<const DawnGpuIndexBuffer*>(ibSubrange.GetBuffer())->GetBuffer(),
                    idxFmt,
                    ibSubrange.GetByteOffset(),
                    ibSubrange.GetItemCount() * idxSize);

                // Send up the model and model-view-projection matrices
                gpuDevice.GetQueue().WriteBuffer(m_WorldAndProjBuf,
                    sizeofAlignedTransforms * meshCount,
                    matrices,
                    sizeof(matrices));

                uint32_t offsets[] =
                {
                    static_cast<uint32_t>(sizeofAlignedTransforms * meshCount),
                    static_cast<uint32_t>(sizeofAlignedMaterialColor * mtlCount)
                };
                renderPass.SetBindGroup(1, m_VertexShaderBindGroup, std::size(offsets), offsets);

                renderPass.DrawIndexed(mesh.GetIndexCount(), 1, 0, 0, 0);

                ++meshCount;
            }

            ++mtlCount;
        }
    }

    drawTimer.Stop();

    renderPass.End();

    //DO NOT SUBMIT
    //cleanupRenderPass.release();

    static PerfTimer resolveTimer("Renderer.Resolve");
    resolveTimer.Start();

    {
        static PerfTimer copyTimer("Renderer.Resolve.CopyColorTarget");
        auto scopedTimer = copyTimer.StartScoped();
        auto copyResult = CopyColorTargetToSwapchain(cmdEncoder, swapchainTextureView);
       expect(copyResult, copyResult.error());
    }

    {
        static PerfTimer renderGuiTimer("Renderer.Resolve.RenderGUI");
        auto scopedTimer = renderGuiTimer.StartScoped();
        auto renderGuiResult = RenderGui(cmdEncoder, swapchainTextureView);
        expect(renderGuiResult, renderGuiResult.error());
    }

    SwapStates();

    //DO NOT SUBMIT
   /* eassert(!m_CurrentState->m_RenderFence, "Render fence should be null here");

    m_CurrentState->m_RenderFence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdBuf);
    expect(m_CurrentState->m_RenderFence, SDL_GetError());*/

    wgpu::CommandBuffer cmd;
    {
        static PerfTimer finishCmdBufferTimer("Renderer.Resolve.FinishCommandBuffer");
        auto scopedTimer = finishCmdBufferTimer.StartScoped();
        cmd = cmdEncoder.Finish(nullptr);
        expect(cmd, "Failed to finish command buffer for render pass");
    }

    {
        static PerfTimer submitCmdBufferTimer("Renderer.Resolve.SubmitCommandBuffer");
        auto scopedTimer = submitCmdBufferTimer.StartScoped();
        wgpu::Queue queue = m_GpuDevice->Device.GetQueue();
        expect(queue, "Failed to get WGPUQueue for render pass");

        queue.Submit(1, &cmd);
    }

    resolveTimer.Stop();

    return Result<void>::Success;
}

//private:

Result<wgpu::RenderPassEncoder>
DawnRenderer::BeginRenderPass(wgpu::CommandEncoder cmdEncoder)
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

    wgpu::RenderPassColorAttachment attachment //
        {
            .view = static_cast<DawnGpuColorTarget*>(m_ColorTarget)->GetTextureView(),
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { 0.0f, 0.0f, 0.0f, 0.0f },
        };

    static constexpr float CLEAR_DEPTH = 1.0f;

    wgpu::RenderPassDepthStencilAttachment depthStencilAttachment //
        {
            .view = static_cast<DawnGpuDepthTarget*>(m_DepthTarget)->GetTextureView(),
            .depthLoadOp = wgpu::LoadOp::Clear,
            .depthStoreOp = wgpu::StoreOp::Store,
            .depthClearValue = CLEAR_DEPTH,
            .stencilLoadOp = wgpu::LoadOp::Undefined,
            .stencilStoreOp = wgpu::StoreOp::Undefined,
            .stencilClearValue = 0,
        };

    wgpu::RenderPassDescriptor renderPassDesc //
        {
            .label = "MainRenderPass",
            .colorAttachmentCount = 1,
            .colorAttachments = &attachment,
            .depthStencilAttachment = &depthStencilAttachment,
        };

    wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);
    expect(renderPass, "Failed to begin render pass");

    //DO NOT SUBMIT
    /*const SDL_GPUViewport viewport
    {
        0, 0, static_cast<float>(targetWidth), static_cast<float>(targetHeight), 0, CLEAR_DEPTH
    };
    SDL_SetGPUViewport(renderPass, &viewport);*/

    return renderPass;
}

//DO NOT SUBMIT
/*void
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
}*/

void
DawnRenderer::SwapStates()
{
    //DO NOT SUBMIT
    //eassert(!m_CurrentState->m_RenderFence, "Current state's render fence must be null when swapping states");

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
DawnRenderer::CopyColorTargetToSwapchain(wgpu::CommandEncoder cmdEncoder, wgpu::TextureView target)
{
    if(!target)
    {
        // Off-screen rendering, skip rendering ImGui
        return Result<void>::Success;
    }

    auto pipelineResult = GetCopyColorTargetPipeline();
    expect(pipelineResult, pipelineResult.error());

    auto pipeline = pipelineResult.value();

    wgpu::RenderPassColorAttachment attachment //
        {
            .view = target,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { 0.0f, 0.0f, 0.0f, 1.0f },
        };

    wgpu::RenderPassDescriptor renderPassDesc //
        {
            .label = "CopyRenderPass",
            .colorAttachmentCount = 1,
            .colorAttachments = &attachment,
        };

    wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);
    expect(renderPass, "Failed to begin render pass for copying color target to swapchain");

    renderPass.SetPipeline(pipeline);
    renderPass.SetBindGroup(2, m_CopyTextureBindGroup, 0, nullptr);
    renderPass.Draw(3, 1, 0, 0);
    renderPass.End();

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

Result<GpuVertexShader*>
DawnRenderer::GetCopyColorTargetVertexShader()
{
    if(m_CopyTextureVertexShader)
    {
        return m_CopyTextureVertexShader;
    }

    std::vector<uint8_t> shaderCode;
    auto loadResult = LoadShaderCode("shaders/Debug/FullScreenTriangle.vs.wgsl", shaderCode);
    expect(loadResult, loadResult.error());

    std::span<uint8_t> shaderCodeSpan(shaderCode.data(), shaderCode.size());

    auto vsResult = m_GpuDevice->CreateVertexShader(shaderCodeSpan);
    expect(vsResult, vsResult.error());

    m_CopyTextureVertexShader = vsResult.value();
    return m_CopyTextureVertexShader;
}

Result<GpuFragmentShader*>
DawnRenderer::GetCopyColorTargetFragmentShader()
{
    if(m_CopyTextureFragmentShader)
    {
        return m_CopyTextureFragmentShader;
    }

    std::vector<uint8_t> shaderCode;
    auto loadResult = LoadShaderCode("shaders/Debug/FullScreenTriangle.fs.wgsl", shaderCode);
    expect(loadResult, loadResult.error());

    std::span<uint8_t> shaderCodeSpan(shaderCode.data(), shaderCode.size());

    auto fsResult = m_GpuDevice->CreateFragmentShader(shaderCodeSpan);
    expect(fsResult, fsResult.error());

    m_CopyTextureFragmentShader = fsResult.value();
    return m_CopyTextureFragmentShader;
}

Result<wgpu::RenderPipeline>
DawnRenderer::GetCopyColorTargetPipeline()
{
    if(m_CopyTexturePipeline)
    {
        return m_CopyTexturePipeline;
    }

    auto vsResult = GetCopyColorTargetVertexShader();
    expect(vsResult, vsResult.error());

    auto fsResult = GetCopyColorTargetFragmentShader();
    expect(fsResult, fsResult.error());

    wgpu::BindGroupLayoutEntry bglEntries[] =//
    {
        {
            .binding = 0,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture =
            {
                .sampleType = wgpu::TextureSampleType::Float,
                .viewDimension = wgpu::TextureViewDimension::e2D,
                .multisampled = false,
            },
        },
        {
            .binding = 1,
            .visibility = wgpu::ShaderStage::Fragment,
            .sampler =
            {
                .type = wgpu::SamplerBindingType::Filtering,
            },
        },
    };

    wgpu::BindGroupLayoutDescriptor bglDesc = //
        {
            .label = "CopyColorTargetBGL",
            .entryCount = std::size(bglEntries),
            .entries = bglEntries,
        };

    m_CopyTextureBindGroupLayout = m_GpuDevice->Device.CreateBindGroupLayout(&bglDesc);
    expect(m_CopyTextureBindGroupLayout, "CreateBindGroupLayout failed for copy color target pipeline");

    wgpu::BindGroupLayout bgl[] = //
        {
            nullptr, // Group 0 unused
            nullptr, // Group 1 unused
            m_CopyTextureBindGroupLayout, // Group 2 for texture and sampler
        };

    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "ColorTargetPipelineLayout",
            .bindGroupLayoutCount = std::size(bgl),
            .bindGroupLayouts = bgl,
        };

    wgpu::PipelineLayout pipelineLayout = m_GpuDevice->Device.CreatePipelineLayout(&pipelineLayoutDesc);
    expect(pipelineLayout, "Failed to create PipelineLayout");

    wgpu::BlendState blendState //
        {
            .color =
            {
                .operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::One,
                .dstFactor = wgpu::BlendFactor::Zero,
            },
            .alpha =
            {
                .operation = wgpu::BlendOperation::Add,
                .srcFactor = wgpu::BlendFactor::One,
                .dstFactor = wgpu::BlendFactor::Zero,
            },
        };

    wgpu::ColorTargetState colorTargetState //
        {
            .format = m_GpuDevice->GetSwapChainFormat(),
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    wgpu::FragmentState fragmentState //
        {
            .module = static_cast<DawnGpuFragmentShader*>(fsResult.value())->GetShader(),
            .entryPoint = "main",
            .targetCount = 1,
            .targets = &colorTargetState,
        };

    wgpu::RenderPipelineDescriptor descriptor//
    {
        .label = "CopyColorTargetPipeline",
        .layout = pipelineLayout,
        .vertex =
        {
            .module = static_cast<DawnGpuVertexShader*>(vsResult.value())->GetShader(),
            .entryPoint = "main",
            .bufferCount = 0,
            .buffers = nullptr,
        },
        .primitive =
        {
            .topology = wgpu::PrimitiveTopology::TriangleList,
            .stripIndexFormat = wgpu::IndexFormat::Undefined,
            .frontFace = wgpu::FrontFace::CW,
            .cullMode = wgpu::CullMode::Back,
            .unclippedDepth = false,
        },
        .depthStencil = nullptr, // No depth/stencil for this pipeline
        .multisample =
        {
            .count = 1,
            .mask = 0xFFFFFFFF,
            .alphaToCoverageEnabled = false,
        },
        .fragment = &fragmentState,
    };

    auto pipeline = m_GpuDevice->Device.CreateRenderPipeline(&descriptor);
    expect(pipeline, "Failed to create render pipeline for copying color target to swapchain");

    // Create bind group for the color target texture and sampler

    wgpu::TextureView texView = static_cast<DawnGpuColorTarget*>(m_ColorTarget)->GetTextureView();
    expect(texView, "Failed to get wgpu::TextureView for color target");

    wgpu::Sampler sampler = static_cast<DawnGpuColorTarget*>(m_ColorTarget)->GetSampler();
    expect(sampler, "Failed to get wgpu::Sampler for color target");

    wgpu::BindGroupEntry bgEntries[] = //
        {
            {
                .binding = 0,
                .textureView = texView,
            },
            {
                .binding = 1,
                .sampler = sampler,
            },
        };

    wgpu::BindGroupDescriptor bgDesc //
        {
            .label = "ColorTargetCopyBindGroup",
            .layout = m_CopyTextureBindGroupLayout,
            .entryCount = std::size(bgEntries),
            .entries = bgEntries,
        };

    wgpu::BindGroup bindGroup = m_GpuDevice->Device.CreateBindGroup(&bgDesc);
    expect(bindGroup, "Failed to create wgpu::BindGroup for copying color target to swapchain");

    m_CopyTextureBindGroup = bindGroup;
    m_CopyTexturePipeline = pipeline;

    return m_CopyTexturePipeline;
}

Result<GpuTexture*>
DawnRenderer::GetDefaultBaseTexture()
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
DawnRenderer::InitGui()
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
    ImGui_ImplSDL3_InitForOther(m_GpuDevice->Window);

    ImGui_ImplWGPU_InitInfo init_info;
    init_info.Device = m_GpuDevice->Device.Get();
    init_info.NumFramesInFlight = 3;
    init_info.RenderTargetFormat = static_cast<WGPUTextureFormat>(m_GpuDevice->GetSwapChainFormat());
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&init_info);

    return Result<void>::Success;
}

Result<void>
DawnRenderer::RenderGui(wgpu::CommandEncoder cmdEncoder, wgpu::TextureView target)
{
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();

    if(!drawData || drawData->TotalVtxCount == 0)
    {
        // Nothing to render for ImGui
        return Result<void>::Success;
    }

    const bool is_minimized = (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f);

    if(is_minimized)
    {
        // Window is minimized, skip rendering ImGui
        return Result<void>::Success;
    }

    if(!target)
    {
        // Off-screen rendering, skip rendering ImGui
        return Result<void>::Success;
    }

    wgpu::RenderPassColorAttachment colorAttachment //
    {
        .view = target,
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = wgpu::LoadOp::Load,
        .storeOp = wgpu::StoreOp::Store,
        .clearValue = { 0.0f, 0.0f, 0.0f, 1.0f },
    };

    wgpu::RenderPassDescriptor renderPassDesc //
    {
        .label = "ImGuiRenderPass",
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment,
        .depthStencilAttachment = nullptr,
    };

    wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);

    ImGui_ImplWGPU_RenderDrawData(drawData, renderPass.Get());

    renderPass.End();

    return Result<void>::Success;
}