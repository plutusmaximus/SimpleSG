#define _CRT_SECURE_NO_WARNINGS

#define __LOGGER_NAME__ "DAWN"

#include "DawnRenderer.h"

#include "Logging.h"

#include "Result.h"
#include "scope_exit.h"

#include "DawnGpuDevice.h"
#include "PerfMetrics.h"

#include <cstdio>

static constexpr const char* COMPOSITE_COLOR_TARGET_VS = "shaders/Debug/FullScreenTriangle.vs.wgsl";
static constexpr const char* COMPOSITE_COLOR_TARGET_FS = "shaders/Debug/FullScreenTriangle.fs.wgsl";

static constexpr const char* COLOR_PIPELINE_VS = "shaders/Debug/VertexShader.vs.wgsl";
static constexpr const char* COLOR_PIPELINE_FS = "shaders/Debug/FragmentShader.fs.wgsl";

class XFormBuffer
{
public:

    Mat44f ModelXform;
    Mat44f ModelViewProjXform;

};

DawnRenderer::DawnRenderer(DawnGpuDevice* gpuDevice)
    : m_GpuDevice(gpuDevice)
{
    gpuDevice->Device.GetLimits(&m_GpuLimits);
}

DawnRenderer::~DawnRenderer()
{
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

    // Nothing to do for the following resources as they are managed by Dawn and will be
    // automatically released

    // m_ColorVertexShader
    // m_ColorFragmentShader
    // m_ColorPipeline
    // m_VsBindGroupLayout
    // m_FsBindGroupLayout
    // m_CopyTextureVertexShader
    // m_CopyTextureFragmentShader
    // m_CopyTexturePipeline
    // m_CopyTextureBindGroupLayout
    // m_CopyTextureBindGroup
    // m_WorldAndProjBuf
    // m_VertexShaderBindGroup
}

void
DawnRenderer::AddModel(const Mat44f& worldTransform, const Model* model)
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
        ++m_CurrentState->m_MeshCount;
    }
}

template<typename T>
static inline size_t alignUniformBuffer(const wgpu::Limits& limits)
{
    const size_t alignment = limits.minUniformBufferOffsetAlignment;

    return (sizeof(T) + alignment - 1) & ~(alignment - 1);
}

Result<void>
DawnRenderer::Render(const Mat44f& camera, const Mat44f& projection, RenderCompositor* compositor)
{
    static PerfTimer renderTimer("Renderer.Render");
    auto scopedRenderTimer = renderTimer.StartScoped();

    auto gpuDevice = m_GpuDevice->Device;

    DawnRenderCompositor* dawnCompositor = static_cast<DawnRenderCompositor*>(compositor);

    wgpu::CommandEncoder cmdEncoder = dawnCompositor->GetCommandEncoder();

    wgpu::RenderPassEncoder renderPass;
    static PerfTimer beginRenderPassTimer("Renderer.Render.BeginRenderPass");
    {
        auto scopedTimer = beginRenderPassTimer.StartScoped();
        auto renderPassResult = BeginRenderPass(cmdEncoder);
        expect(renderPassResult, renderPassResult.error());

        renderPass = renderPassResult.value();
    }

    static PerfTimer setPipelineTimer("Renderer.Render.SetPipeline");
    {
        auto scopedTimer = setPipelineTimer.StartScoped();

        auto pipelineResult = GetColorPipeline();
        expect(pipelineResult, pipelineResult.error());
        auto pipeline = pipelineResult.value();

        renderPass.SetPipeline(pipeline);
    }

    static PerfTimer updateXformTimer("Renderer.Render.UpdateXformBuffer");
    {
        auto scopedTimer = updateXformTimer.StartScoped();

        auto updateXformBufResult = UpdateXformBuffer(cmdEncoder, camera, projection);
        expect(updateXformBufResult, updateXformBufResult.error());
    }

    static PerfTimer setVsBindGroupTimer("Renderer.Render.Draw.SetVsBindGroup");
    {
        auto scopedTimer = setVsBindGroupTimer.StartScoped();
        renderPass.SetBindGroup(0, m_VertexShaderBindGroup, 0, nullptr);
    }

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

    const DawnGpuVertexBuffer* lastVb = nullptr;
    const DawnGpuIndexBuffer* lastIb = nullptr;

    unsigned meshCount = 0;

    for(const auto meshGrpPtr : meshGroups)
    {
        for (auto& [mtlId, xmeshes] : *meshGrpPtr)
        {
            GpuMaterial* gpuMtl = xmeshes[0].MeshInstance.GetGpuMaterial();

            static PerfTimer fsBindingTimer("Renderer.Render.Draw.SetMaterialBindGroup");
            {
                auto scopedTimer = fsBindingTimer.StartScoped();
                DawnGpuMaterial* dawnMtl = static_cast<DawnGpuMaterial*>(gpuMtl);
                wgpu::BindGroup bindGroup = dawnMtl->GetBindGroup();
                renderPass.SetBindGroup(2, bindGroup, 0, nullptr);
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

                const auto& vbSubrange = mesh.GetVertexBuffer();
                const auto& ibSubrange = mesh.GetIndexBuffer();

                static_assert(VERTEX_INDEX_BITS == 32 || VERTEX_INDEX_BITS == 16);

                constexpr wgpu::IndexFormat idxFmt =
                    (VERTEX_INDEX_BITS == 32)
                    ? wgpu::IndexFormat::Uint32
                    : wgpu::IndexFormat::Uint16;

                constexpr unsigned idxSize = (VERTEX_INDEX_BITS == 32) ? sizeof(uint32_t) : sizeof(uint16_t);

                auto vb = static_cast<const DawnGpuVertexBuffer*>(vbSubrange.GetBuffer());
                auto ib = static_cast<const DawnGpuIndexBuffer*>(ibSubrange.GetBuffer());

                static PerfTimer setBuffersTimer("Renderer.Render.Draw.SetBuffers");
                if(lastVb != vb || lastIb != ib)
                {
                    auto scopedTimer = setBuffersTimer.StartScoped();

                    renderPass.SetVertexBuffer(0,
                        vb->GetBuffer(),
                        0,
                        vb->GetVertexCount() * sizeof(Vertex));

                    renderPass.SetIndexBuffer(ib->GetBuffer(),
                        idxFmt,
                        0,
                        ib->GetIndexCount() * idxSize);

                    lastVb = vb;
                    lastIb = ib;
                }

                /*static PerfTimer writeTransformTimer("Renderer.Render.Draw.WriteTransformBuffer");
                {
                    auto scopedTimer = writeTransformTimer.StartScoped();
                    // Send up the model and model-view-projection matrices
                    gpuDevice.GetQueue().WriteBuffer(m_WorldAndProjBuf,
                        sizeof(XFormBuffer) * meshCount,
                        matrices,
                        sizeof(matrices));
                }*/

                static PerfTimer drawIndexedTimer("Renderer.Render.Draw.DrawIndexed");
                {
                    auto scopedTimer = drawIndexedTimer.StartScoped();
                    renderPass.DrawIndexed(mesh.GetIndexCount(),
                        1,
                        ibSubrange.GetIndexOffset(),
                        vbSubrange.GetVertexOffset(),
                        meshCount);
                }

                ++meshCount;
            }
        }
    }

    drawTimer.Stop();

    renderPass.End();

    static PerfTimer resolveTimer("Renderer.Render.Resolve");
    resolveTimer.Start();

    static PerfTimer copyTimer("Renderer.Render.Resolve.CopyColorTarget");
    {
        auto scopedTimer = copyTimer.StartScoped();
        auto copyResult = CopyColorTargetToSwapchain(cmdEncoder, dawnCompositor->GetTarget());
        expect(copyResult, copyResult.error());
    }

    SwapStates();

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

void
DawnRenderer::SwapStates()
{
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
        // Off-screen rendering, skip rendering to swapchain
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

Result<wgpu::ShaderModule>
DawnRenderer::GetColorVertexShader()
{
    if(m_ColorVertexShader)
    {
        return m_ColorVertexShader;
    }

    auto vsResult = CreateVertexShader(COLOR_PIPELINE_VS);
    expect(vsResult, vsResult.error());

    m_ColorVertexShader = vsResult.value();
    return m_ColorVertexShader;
}

Result<wgpu::ShaderModule>
DawnRenderer::GetColorFragmentShader()
{
    if(m_ColorFragmentShader)
    {
        return m_ColorFragmentShader;
    }

    auto fsResult = CreateFragmentShader(COLOR_PIPELINE_FS);
    expect(fsResult, fsResult.error());

    m_ColorFragmentShader = fsResult.value();
    return m_ColorFragmentShader;
}

Result<wgpu::RenderPipeline>
DawnRenderer::GetColorPipeline()
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

    // Bind group 1 is for vertex shaders.
    wgpu::BindGroupLayoutEntry vertBglEntries[] =//
    {
        {
            /*
                struct XForm
                {
                    modelXform: mat4x4<f32>,
                    modelViewProjXform: mat4x4<f32>,
                };
            */
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex,
            .buffer =
            {
                .type = wgpu::BufferBindingType::ReadOnlyStorage,
                .hasDynamicOffset = false,
                .minBindingSize = sizeof(XFormBuffer),
            },
        },
    };

    wgpu::BindGroupLayoutDescriptor vertBglDesc = //
        {
            .label = "ColorTargetVertBGL",
            .entryCount = std::size(vertBglEntries),
            .entries = vertBglEntries,
        };

    m_VsBindGroupLayout = m_GpuDevice->Device.CreateBindGroupLayout(&vertBglDesc);
    expect(m_VsBindGroupLayout, "Failed to create BindGroupLayout");

    // Bind group 2 is for fragment shaders.
    wgpu::BindGroupLayoutEntry fragBglEntries[] =//
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
        /*
            MaterialConstants
        */
        {
            .binding = 2,
            .visibility = wgpu::ShaderStage::Fragment,
            .buffer =
            {
                .type = wgpu::BufferBindingType::Uniform,
                .hasDynamicOffset = false,
                .minBindingSize = alignUniformBuffer<MaterialConstants>(m_GpuLimits),
            },
        }
    };

    wgpu::BindGroupLayoutDescriptor fragBglDesc = //
        {
            .label = "ColorTargetFragBGL",
            .entryCount = std::size(fragBglEntries),
            .entries = fragBglEntries,
        };

    m_FsBindGroupLayout = m_GpuDevice->Device.CreateBindGroupLayout(&fragBglDesc);
    expect(m_FsBindGroupLayout, "Failed to create BindGroupLayout");

    wgpu::BindGroupLayout bgl[] = //
        {
            m_VsBindGroupLayout,
            nullptr, // Group 1 unused
            m_FsBindGroupLayout,
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
                .srcFactor = wgpu::BlendFactor::SrcAlpha,
                .dstFactor = wgpu::BlendFactor::OneMinusSrcAlpha,
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
            .format = static_cast<DawnGpuColorTarget*>(m_ColorTarget)->GetFormat(),
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    wgpu::DepthStencilState depthStencilState //
        {
            .format = static_cast<DawnGpuDepthTarget*>(m_DepthTarget)->GetFormat(),
            .depthWriteEnabled = true,
            .depthCompare = wgpu::CompareFunction::Less,
            /*.stencilFront =
            {
                .compare = wgpu::CompareFunction::Always,
                .failOp = wgpu::StencilOperation::Keep,
                .depthFailOp = wgpu::StencilOperation::Keep,
                .passOp = wgpu::StencilOperation::Keep,
            },
            .stencilBack =
            {
                .compare = wgpu::CompareFunction::Always,
                .failOp = wgpu::StencilOperation::Keep,
                .depthFailOp = wgpu::StencilOperation::Keep,
                .passOp = wgpu::StencilOperation::Keep,
            },
            .stencilReadMask = 0xFF,
            .stencilWriteMask = 0xFF,*/
            .depthBias = 0,
            .depthBiasSlopeScale = 0.0f,
            .depthBiasClamp = 0.0f,
        };

    wgpu::FragmentState fragmentState //
        {
            .module = fragmentShader,
            .entryPoint = "main",
            .targetCount = 1,
            .targets = &colorTargetState,
        };

    wgpu::VertexAttribute vertexAttributes[] //
        {
            {
                .format = wgpu::VertexFormat::Float32x3,
                .offset = offsetof(Vertex, pos),
                .shaderLocation = 0,
            },
            {
                .format = wgpu::VertexFormat::Float32x3,
                .offset = offsetof(Vertex, normal),
                .shaderLocation = 1,
            },
            {
                .format = wgpu::VertexFormat::Float32x2,
                .offset = offsetof(Vertex, uvs[0]),
                .shaderLocation = 2,
            },
        };
    wgpu::VertexBufferLayout vertexBufferLayout //
        {
            .stepMode = wgpu::VertexStepMode::Vertex,
            .arrayStride = sizeof(Vertex),
            .attributeCount = std::size(vertexAttributes),
            .attributes = vertexAttributes,
        };

    wgpu::RenderPipelineDescriptor descriptor//
    {
        .label = "ColorTargetPipeline",
        .layout = pipelineLayout,
        .vertex =
        {
            .module = vertexShader,
            .entryPoint = "main",
            .bufferCount = 1,
            .buffers = &vertexBufferLayout,
        },
        .primitive =
        {
            .topology = wgpu::PrimitiveTopology::TriangleList,
            .stripIndexFormat = wgpu::IndexFormat::Undefined,
            .frontFace = wgpu::FrontFace::CW,
            .cullMode = wgpu::CullMode::Back,
            .unclippedDepth = false,
        },
        .depthStencil = &depthStencilState,
        .multisample =
        {
            .count = 1,
            .mask = 0xFFFFFFFF,
            .alphaToCoverageEnabled = false,
        },
        .fragment = &fragmentState,
    };

    m_ColorPipeline = m_GpuDevice->Device.CreateRenderPipeline(&descriptor);
    expect(m_ColorPipeline, "Failed to create render pipeline");

    return m_ColorPipeline;

    /*m_Device.CreateRenderPipelineAsync(
        &descriptor,
        wgpu::CallbackMode::AllowProcessEvents,
        +[](wgpu::CreatePipelineAsyncStatus status,
                wgpu::RenderPipeline pipeline,
                wgpu::StringView message,
                CreateRenderPipelineOp *self)
        { self->OnPipelineCreated(status, pipeline, message); },
        this);*/
}

Result<wgpu::ShaderModule>
DawnRenderer::GetCopyColorTargetVertexShader()
{
    if(m_CopyTextureVertexShader)
    {
        return m_CopyTextureVertexShader;
    }

    auto vsResult = CreateVertexShader(COMPOSITE_COLOR_TARGET_VS);
    expect(vsResult, vsResult.error());

    m_CopyTextureVertexShader = vsResult.value();
    return m_CopyTextureVertexShader;
}

Result<wgpu::ShaderModule>
DawnRenderer::GetCopyColorTargetFragmentShader()
{
    if(m_CopyTextureFragmentShader)
    {
        return m_CopyTextureFragmentShader;
    }

    auto fsResult = CreateFragmentShader(COMPOSITE_COLOR_TARGET_FS);
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
            .module = fsResult.value(),
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
            .module = vsResult.value(),
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

Result<wgpu::ShaderModule>
DawnRenderer::CreateVertexShader(const char* path)
{
    std::vector<uint8_t> shaderCode;
    auto loadResult = LoadShaderCode(path, shaderCode);
    expect(loadResult, loadResult.error());

    wgpu::StringView shaderCodeView{ reinterpret_cast<const char*>(shaderCode.data()),
        shaderCode.size() };
    wgpu::ShaderSourceWGSL wgsl{ { .code = shaderCodeView } };
    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{ .nextInChain = &wgsl };

    wgpu::ShaderModule shaderModule = m_GpuDevice->Device.CreateShaderModule(&shaderModuleDescriptor);
    expect(shaderModule, "Failed to create shader module");

    return shaderModule;
}

Result<wgpu::ShaderModule>
DawnRenderer::CreateFragmentShader(const char* path)
{
    std::vector<uint8_t> shaderCode;
    auto loadResult = LoadShaderCode(path, shaderCode);
    expect(loadResult, loadResult.error());

    wgpu::StringView shaderCodeView{ reinterpret_cast<const char*>(shaderCode.data()),
        shaderCode.size() };
    wgpu::ShaderSourceWGSL wgsl{ { .code = shaderCodeView } };
    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{ .nextInChain = &wgsl };

    wgpu::ShaderModule shaderModule = m_GpuDevice->Device.CreateShaderModule(&shaderModuleDescriptor);
    expect(shaderModule, "Failed to create shader module");

    return shaderModule;
}

Result<void>
DawnRenderer::UpdateXformBuffer(
    wgpu::CommandEncoder /*cmdEncoder*/, const Mat44f& camera, const Mat44f& projection)
{
    // Size of the buffer needed to hold the world and projection matrices for all meshes in the
    // current frame.
    const size_t sizeofTransformBuffer = sizeof(XFormBuffer) * m_CurrentState->m_MeshCount;

    if(!m_WorldAndProjBuf || !m_WorldAndProjXferBuf ||
        m_SizeofTransformBuffer < sizeofTransformBuffer)
    {
        // Re-allocate the world and projection buffer.

        m_SizeofTransformBuffer = sizeofTransformBuffer;

        wgpu::BufferDescriptor worldAndProjBufDesc //
        {
            .label = "WorldAndProjection",
            .usage = wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
            .size = sizeofTransformBuffer,
            .mappedAtCreation = false,
        };

        m_WorldAndProjBuf = m_GpuDevice->Device.CreateBuffer(&worldAndProjBufDesc);
        expect(m_WorldAndProjBuf, "Failed to create world and projection buffer");

        wgpu::BufferDescriptor xferBufDesc //
        {
            .label = "WorldAndProjectionXfer",
            .usage = wgpu::BufferUsage::MapWrite | wgpu::BufferUsage::CopySrc,
            .size = sizeofTransformBuffer,
            .mappedAtCreation = false,
        };

        m_WorldAndProjXferBuf = m_GpuDevice->Device.CreateBuffer(&xferBufDesc);
        expect(m_WorldAndProjXferBuf, "Failed to create world and projection transfer buffer");

        // Recreate the vertex shader bind group with the new buffer.
        wgpu::BindGroupEntry vsBgEntries[] = //
            {
                {
                    .binding = 0,
                    .buffer = m_WorldAndProjBuf,
                    .offset = 0,
                    .size = sizeofTransformBuffer,
                }
            };

        wgpu::BindGroupDescriptor vsBgDesc //
            {
                .label = "vsBindGroup",
                .layout = m_VsBindGroupLayout,
                .entryCount = std::size(vsBgEntries),
                .entries = vsBgEntries,
            };

        m_VertexShaderBindGroup = m_GpuDevice->Device.CreateBindGroup(&vsBgDesc);
        expect(m_VertexShaderBindGroup, "Failed to create WGPUBindGroup");
    }

    /*m_WorldAndProjXferBuf.MapAsync(wgpu::MapMode::Write, 0, m_SizeofTransformBuffer);

    XFormBuffer* mappedData = reinterpret_cast<XFormBuffer*>(
        m_WorldAndProjXferBuf.GetMappedRange(0, m_SizeofTransformBuffer));*/

    const MeshGroupCollection* meshGroups[] =
    {
        &m_CurrentState->m_OpaqueMeshGroups,
        &m_CurrentState->m_TranslucentMeshGroups
    };

    // Use inverse of camera transform as view matrix
    const Mat44f viewXform = camera.Inverse();

    // Projection transform
    const Mat44f viewProj = projection.Mul(viewXform);

    unsigned meshCount = 0;

    for(const auto meshGrpPtr : meshGroups)
    {
        for (auto& [mtlId, xmeshes] : *meshGrpPtr)
        {
            for (auto& xmesh : xmeshes)
            {
                XFormBuffer matrices =
                {
                    .ModelXform = xmesh.WorldTransform,
                    .ModelViewProjXform = viewProj.Mul(xmesh.WorldTransform),
                };

                m_GpuDevice->Device.GetQueue().WriteBuffer(m_WorldAndProjBuf,
                        sizeof(XFormBuffer) * meshCount,
                        &matrices,
                        sizeof(matrices));

                /*mappedData[meshCount] =//
                {
                    .ModelXform = xmesh.WorldTransform,
                    .ModelViewProjXform = viewProj.Mul(xmesh.WorldTransform),
                };*/

                ++meshCount;
            }
        }
    }

    /*m_WorldAndProjXferBuf.Unmap();

    uint64_t srcOffset = 0;
    uint64_t dstOffset = 0;
    uint64_t numBytes  = m_SizeofTransformBuffer;

    cmdEncoder.CopyBufferToBuffer(m_WorldAndProjXferBuf,
        srcOffset,
        m_WorldAndProjBuf,
        dstOffset,
        numBytes);*/

    return Result<void>::Success;
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