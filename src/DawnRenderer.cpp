#define _CRT_SECURE_NO_WARNINGS

#define __LOGGER_NAME__ "DAWN"

#include "DawnRenderer.h"

#include "Log.h"

#include "Result.h"
#include "scope_exit.h"

#include "DawnGpuDevice.h"
#include "PerfMetrics.h"

#include <cstdio>

static constexpr const char* COMPOSITE_COLOR_TARGET_VS = "shaders/FullScreenTriangle.vs.wgsl";
static constexpr const char* COMPOSITE_COLOR_TARGET_FS = "shaders/FullScreenTriangle.fs.wgsl";

static constexpr const char* COLOR_PIPELINE_VS = "shaders/VertexShader.vs.wgsl";
static constexpr const char* COLOR_PIPELINE_FS = "shaders/FragmentShader.fs.wgsl";

static constexpr const char* TRANSFORM_SHADER_CS = "shaders/TransformShader.cs.wgsl";

static Result<>
CreateTransformBindGroups(wgpu::Device device,
    wgpu::ComputePipeline pipeline,
    wgpu::Buffer worldSpaceBuffer,
    wgpu::Buffer clipSpaceBuffer,
    wgpu::Buffer viewProjBuffer,
    wgpu::BindGroup* bindGroups);

static Result<wgpu::Buffer>
CreateBuffer(wgpu::Device device, wgpu::BufferUsage usage, size_t size, const char* label);

namespace
{
    class DrawIndirectBufferParams
    {
    public:
        uint32_t IndexCount;
        uint32_t InstanceCount;
        uint32_t FirstIndex;
        uint32_t BaseVertex;
        uint32_t FirstInstance;
    };
}

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
            MLG_ERROR("Failed to destroy default base texture");
        }
    }

    if(m_ColorTarget)
    {
        auto result = m_GpuDevice->DestroyColorTarget(m_ColorTarget);
        if(!result)
        {
            MLG_ERROR("Failed to destroy default color target");
        }
    }

    if(m_DepthTarget)
    {
        auto result = m_GpuDevice->DestroyDepthTarget(m_DepthTarget);
        if(!result)
        {
            MLG_ERROR("Failed to destroy default depth target");
        }
    }
}

template<typename T>
static inline size_t alignUniformBuffer(const wgpu::Limits& limits)
{
    const size_t alignment = limits.minUniformBufferOffsetAlignment;

    return (sizeof(T) + alignment - 1) & ~(alignment - 1);
}

Result<>
DawnRenderer::Render(const Mat44f& camera,
    const Mat44f& projection,
    const Model* model,
    RenderCompositor* compositor)
{
    MLG_CHECK(CreateColorAndDepthTargets());
    MLG_CHECK(CreateColorPipeline());
    MLG_CHECK(CreateBltPipeline());
    MLG_CHECK(CreateTransformPipeline());

    static PerfTimer renderTimer("Renderer.Render");
    auto scopedRenderTimer = renderTimer.StartScoped();

    auto gpuDevice = m_GpuDevice->Device;

    DawnRenderCompositor* dawnCompositor = static_cast<DawnRenderCompositor*>(compositor);

    wgpu::CommandEncoder cmdEncoder = dawnCompositor->GetCommandEncoder();

    static PerfTimer updateXformTimer("Renderer.Render.UpdateXformBuffer");
    {
        auto scopedTimer = updateXformTimer.StartScoped();

        auto updateXformBufResult = UpdateXformBuffer(cmdEncoder, camera, projection, model);
        MLG_CHECK(updateXformBufResult);
    }

    wgpu::RenderPassEncoder renderPass;
    static PerfTimer beginRenderPassTimer("Renderer.Render.BeginRenderPass");
    {
        auto scopedTimer = beginRenderPassTimer.StartScoped();
        auto renderPassResult = BeginRenderPass(cmdEncoder);
        MLG_CHECK(renderPassResult);

        renderPass = *renderPassResult;
    }

    static PerfTimer setPipelineTimer("Renderer.Render.SetPipeline");
    {
        auto scopedTimer = setPipelineTimer.StartScoped();

        renderPass.SetPipeline(m_ColorPipeline.Pipeline);
    }

    static PerfTimer setVsBindGroupTimer("Renderer.Render.Draw.SetVsBindGroup");
    {
        auto scopedTimer = setVsBindGroupTimer.StartScoped();
        renderPass.SetBindGroup(0, m_ColorPipeline.BindGroup0, 0, nullptr);
        renderPass.SetBindGroup(1, m_ColorPipeline.BindGroup1, 0, nullptr);
    }

    static PerfTimer setBuffersTimer("Renderer.Render.Draw.SetBuffers");
    {
        auto scopedTimer = setBuffersTimer.StartScoped();

        static_assert(VERTEX_INDEX_BITS == 32 || VERTEX_INDEX_BITS == 16);

        constexpr wgpu::IndexFormat idxFmt =
            (VERTEX_INDEX_BITS == 32)
            ? wgpu::IndexFormat::Uint32
            : wgpu::IndexFormat::Uint16;

        auto vb = static_cast<const DawnGpuVertexBuffer*>(model->GetGpuVertexBuffer());
        auto ib = static_cast<const DawnGpuIndexBuffer*>(model->GetGpuIndexBuffer());

        renderPass.SetVertexBuffer(0,
            vb->GetBuffer(),
            0,
            vb->GetBuffer().GetSize());

        renderPass.SetIndexBuffer(ib->GetBuffer(),
            idxFmt,
            0,
            ib->GetBuffer().GetSize());
    }

    static PerfTimer drawTimer("Renderer.Render.Draw");
    drawTimer.Start();

    unsigned meshCount = 0;

    const auto& meshes = model->GetMeshes();

    auto drawIndirectBuf =
        static_cast<const DawnGpuDrawIndirectBuffer*>(model->GetDrawIndirectBuffer())
            ->GetBuffer();

    for(size_t i = 0; i < meshes.size(); ++i)
    {
        const Mesh& mesh = meshes[i];
        const GpuMaterial* gpuMtl = mesh.GetGpuMaterial();

        static PerfTimer fsBindingTimer("Renderer.Render.Draw.SetMaterialBindGroup");
        {
            auto scopedTimer = fsBindingTimer.StartScoped();
            const DawnGpuMaterial* dawnMtl = static_cast<const DawnGpuMaterial*>(gpuMtl);
            wgpu::BindGroup bindGroup = dawnMtl->GetBindGroup();
            renderPass.SetBindGroup(2, bindGroup, 0, nullptr);
        }

        static PerfTimer drawIndexedTimer("Renderer.Render.Draw.DrawIndexed");
        {
            auto scopedTimer = drawIndexedTimer.StartScoped();
            /*renderPass.DrawIndexed(mesh.GetIndexCount(),
                1,
                mesh.GetIndexOffset(),
                mesh.GetVertexOffset(),
                meshCount);*/

            renderPass.DrawIndexedIndirect(drawIndirectBuf,
                sizeof(DrawIndirectBufferParams) * meshCount);
        }

        ++meshCount;
    }

    drawTimer.Stop();

    renderPass.End();

    static PerfTimer resolveTimer("Renderer.Render.Resolve");
    resolveTimer.Start();

    static PerfTimer copyTimer("Renderer.Render.Resolve.CopyColorTarget");
    {
        auto scopedTimer = copyTimer.StartScoped();
        auto copyResult = CopyColorTargetToSwapchain(cmdEncoder, dawnCompositor->GetTarget());
        MLG_CHECK(copyResult);
    }

    resolveTimer.Stop();

    return Result<>::Ok;
}

//private:

Result<wgpu::RenderPassEncoder>
DawnRenderer::BeginRenderPass(wgpu::CommandEncoder cmdEncoder)
{
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
    MLG_CHECK(renderPass, "Failed to begin render pass");

    //DO NOT SUBMIT
    /*const SDL_GPUViewport viewport
    {
        0, 0, static_cast<float>(targetWidth), static_cast<float>(targetHeight), 0, CLEAR_DEPTH
    };
    SDL_SetGPUViewport(renderPass, &viewport);*/

    return renderPass;
}

Result<>
DawnRenderer::CopyColorTargetToSwapchain(wgpu::CommandEncoder cmdEncoder, wgpu::TextureView target)
{
    if(!target)
    {
        // Off-screen rendering, skip rendering to swapchain
        return Result<>::Ok;
    }

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
    MLG_CHECK(renderPass, "Failed to begin render pass for copying color target to swapchain");

    renderPass.SetPipeline(m_BltPipeline.Pipeline);
    renderPass.SetBindGroup(2, m_BltPipeline.BindGroup2, 0, nullptr);
    renderPass.Draw(3, 1, 0, 0);
    renderPass.End();

    return Result<>::Ok;
}

static Result<>
LoadShaderCode(const char* filePath, std::vector<uint8_t>& outBuffer)
{
    FILE* fp = std::fopen(filePath, "rb");
    MLG_CHECK(fp, "Failed to open shader file: {} ({})", filePath, std::strerror(errno));

    auto cleanupFile = scope_exit([&]() { std::fclose(fp); });

    //Get file size
    MLG_CHECK(std::fseek(fp, 0, SEEK_END) == 0,
        "Failed to seek in shader file: {} ({})",
        filePath,
        std::strerror(errno));

    long fileSize = std::ftell(fp);
    MLG_CHECK(fileSize >= 0,
        "Failed to get size of shader file: {} ({})",
        filePath,
        std::strerror(errno));
    std::rewind(fp);

    outBuffer.resize(static_cast<size_t>(fileSize));

    MLG_CHECK(std::fread(outBuffer.data(), 1, static_cast<size_t>(fileSize), fp) ==
                static_cast<size_t>(fileSize),
            "Failed to read shader file: {} ({})", filePath, std::strerror(errno));

    return Result<>::Ok;
}

Result<>
DawnRenderer::CreateColorAndDepthTargets()
{
    const auto screenBounds = m_GpuDevice->GetScreenBounds();

    const unsigned targetWidth = static_cast<unsigned>(screenBounds.Width);
    const unsigned targetHeight = static_cast<unsigned>(screenBounds.Height);

    if(!m_ColorTarget || m_ColorTarget->GetWidth() != targetWidth ||
        m_ColorTarget->GetHeight() != targetHeight)
    {
        MLG_DEBUG("Creating new color target for render pass with size {}x{}", targetWidth, targetHeight);

        if(m_ColorTarget)
        {
            auto result = m_GpuDevice->DestroyColorTarget(m_ColorTarget);
            if(!result)
            {
                MLG_ERROR("Failed to destroy default color target");
            }
            m_ColorTarget = nullptr;
        }

        auto result = m_GpuDevice->CreateColorTarget(targetWidth, targetHeight, "ColorTarget");
        MLG_CHECK(result);
        m_ColorTarget = *result;
    }

    if(!m_DepthTarget || m_DepthTarget->GetWidth() != targetWidth ||
        m_DepthTarget->GetHeight() != targetHeight)
    {
        MLG_DEBUG("Creating new depth target for render pass with size {}x{}", targetWidth, targetHeight);

        if(m_DepthTarget)
        {
            auto result = m_GpuDevice->DestroyDepthTarget(m_DepthTarget);
            if(!result)
            {
                MLG_ERROR("Failed to destroy default depth target");
            }
            m_DepthTarget = nullptr;
        }

        auto result = m_GpuDevice->CreateDepthTarget(targetWidth, targetHeight, "DepthTarget");
        MLG_CHECK(result);
        m_DepthTarget = *result;
    }

    return Result<>::Ok;
}

Result<>
DawnRenderer::CreateColorPipeline()
{
    if(m_ColorPipeline.Pipeline)
    {
        return Result<>::Ok;
    }

    MLG_CHECKV(m_ColorTarget, "Color target is null");

    auto vsResult = CreateShader(COLOR_PIPELINE_VS);
    MLG_CHECK(vsResult);

    m_ColorPipeline.VertexShader = *vsResult;

    auto fsResult = CreateShader(COLOR_PIPELINE_FS);
    MLG_CHECK(fsResult);

    m_ColorPipeline.FragmentShader = *fsResult;

    // Color pipeline bind group 0 layout
    wgpu::BindGroupLayoutEntry bgl0Entries[] =//
    {
        // World space transform.
        {
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex,
            .buffer =
            {
                .type = wgpu::BufferBindingType::ReadOnlyStorage,
                .hasDynamicOffset = false,
                .minBindingSize = sizeof(Mat44f),
            },
        },
        //Mesh to transform index mapping
        {
            .binding = 1,
            .visibility = wgpu::ShaderStage::Vertex,
            .buffer =
            {
                .type = wgpu::BufferBindingType::ReadOnlyStorage,
                .hasDynamicOffset = false,
                .minBindingSize = sizeof(TransformIndex),
            },
        },
    };
    wgpu::BindGroupLayoutDescriptor bgl0Desc = //
        {
            .label = "ColorPipelineBg0Layout",
            .entryCount = std::size(bgl0Entries),
            .entries = bgl0Entries,
        };

    // Color pipeline bind group 1 layout
    wgpu::BindGroupLayoutEntry bgl1Entries[] =//
    {
        // Clip space transform
        {
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex,
            .buffer =
            {
                .type = wgpu::BufferBindingType::ReadOnlyStorage,
                .hasDynamicOffset = false,
                .minBindingSize = sizeof(Mat44f),
            },
        },
    };
    wgpu::BindGroupLayoutDescriptor bgl1Desc = //
        {
            .label = "ColorPipelineBg1Layout",
            .entryCount = std::size(bgl1Entries),
            .entries = bgl1Entries,
        };

    // Color pipeline bind group 2 layout
    wgpu::BindGroupLayoutEntry bgl2Entries[] =//
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

    wgpu::BindGroupLayoutDescriptor bgl2Desc = //
        {
            .label = "ColorPipelineBg2Layout",
            .entryCount = std::size(bgl2Entries),
            .entries = bgl2Entries,
        };

    m_ColorPipeline.BindGroup0Layout = m_GpuDevice->Device.CreateBindGroupLayout(&bgl0Desc);
    MLG_CHECK(m_ColorPipeline.BindGroup0Layout,
        "Failed to create bind group 0 layout for color pipeline");

    m_ColorPipeline.BindGroup1Layout = m_GpuDevice->Device.CreateBindGroupLayout(&bgl1Desc);
    MLG_CHECK(m_ColorPipeline.BindGroup1Layout,
        "Failed to create bind group 1 layout for color pipeline");

    m_ColorPipeline.BindGroup2Layout = m_GpuDevice->Device.CreateBindGroupLayout(&bgl2Desc);
    MLG_CHECK(m_ColorPipeline.BindGroup2Layout,
        "Failed to create bind group 2 layout for color pipeline");

    // Color target pipeline layout

    wgpu::BindGroupLayout colorTargetBgl[] = //
        {
            m_ColorPipeline.BindGroup0Layout,
            m_ColorPipeline.BindGroup1Layout,
            m_ColorPipeline.BindGroup2Layout,
        };

    wgpu::PipelineLayoutDescriptor colorTargetPipelineLayoutDesc //
        {
            .label = "ColorPipelineLayout",
            .bindGroupLayoutCount = std::size(colorTargetBgl),
            .bindGroupLayouts = colorTargetBgl,
        };

    m_ColorPipeline.PipelineLayout =
        m_GpuDevice->Device.CreatePipelineLayout(&colorTargetPipelineLayoutDesc);
    MLG_CHECK(m_ColorPipeline.PipelineLayout, "Failed to create color pipeline layout");

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
            .module = m_ColorPipeline.FragmentShader,
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
        .layout = m_ColorPipeline.PipelineLayout,
        .vertex =
        {
            .module = m_ColorPipeline.VertexShader,
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

    m_ColorPipeline.Pipeline = m_GpuDevice->Device.CreateRenderPipeline(&descriptor);
    MLG_CHECK(m_ColorPipeline.Pipeline, "Failed to create render pipeline");

    return Result<>::Ok;

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

Result<>
DawnRenderer::CreateBltPipeline()
{
    if(m_BltPipeline.Pipeline)
    {
        return Result<>::Ok;
    }

    auto vsResult = CreateShader(COMPOSITE_COLOR_TARGET_VS);
    MLG_CHECK(vsResult);

    m_BltPipeline.VertexShader = *vsResult;

    auto fsResult = CreateShader(COMPOSITE_COLOR_TARGET_FS);
    MLG_CHECK(fsResult);

    m_BltPipeline.FragmentShader = *fsResult;

    // BLT pipeline bind group 2 layout
    wgpu::BindGroupLayoutEntry bgl2Entries[] =//
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

    wgpu::BindGroupLayoutDescriptor bgl2Desc = //
        {
            .label = "BltBg2Layout",
            .entryCount = std::size(bgl2Entries),
            .entries = bgl2Entries,
        };

    m_BltPipeline.BindGroup2Layout = m_GpuDevice->Device.CreateBindGroupLayout(&bgl2Desc);
    MLG_CHECK(m_BltPipeline.BindGroup2Layout,
        "Failed to create bind group 2 layout for BLT pipeline");

    // BLT pipeline bind group layout

    wgpu::BindGroupLayout bltBgl[] = //
        {
            nullptr,    //bind group 0
            nullptr,    //bind group 1
            m_BltPipeline.BindGroup2Layout,
        };

    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "BltPipelineLayout",
            .bindGroupLayoutCount = std::size(bltBgl),
            .bindGroupLayouts = bltBgl,
        };

    m_BltPipeline.PipelineLayout = m_GpuDevice->Device.CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(m_BltPipeline.PipelineLayout, "Failed to create BLT pipeline layout");

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
            .module = m_BltPipeline.FragmentShader,
            .entryPoint = "main",
            .targetCount = 1,
            .targets = &colorTargetState,
        };

    wgpu::RenderPipelineDescriptor descriptor//
    {
        .label = "CopyColorTargetPipeline",
        .layout = m_BltPipeline.PipelineLayout,
        .vertex =
        {
            .module = m_BltPipeline.VertexShader,
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

    // Create bind group for the color target texture and sampler

    wgpu::TextureView texView = static_cast<DawnGpuColorTarget*>(m_ColorTarget)->GetTextureView();
    MLG_CHECK(texView, "Failed to get wgpu::TextureView for color target");

    wgpu::Sampler sampler = static_cast<DawnGpuColorTarget*>(m_ColorTarget)->GetSampler();
    MLG_CHECK(sampler, "Failed to get wgpu::Sampler for color target");

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
            .layout = m_BltPipeline.BindGroup2Layout,
            .entryCount = std::size(bgEntries),
            .entries = bgEntries,
        };

    m_BltPipeline.BindGroup2 = m_GpuDevice->Device.CreateBindGroup(&bgDesc);
    MLG_CHECK(m_BltPipeline.BindGroup2, "Failed to create bind group 2 for BLT pipeline");

    m_BltPipeline.Pipeline = m_GpuDevice->Device.CreateRenderPipeline(&descriptor);
    MLG_CHECK(m_BltPipeline.Pipeline, "Failed to create render pipeline for BLT pipeline");

    return Result<>::Ok;
}

Result<>
DawnRenderer::CreateTransformPipeline()
{
    if(m_TransformPipeline)
    {
        return Result<>::Ok;
    }

    auto csResult = CreateShader(TRANSFORM_SHADER_CS);
    MLG_CHECK(csResult);

    m_TransformShader = *csResult;

    // Bind group 0 layout
    wgpu::BindGroupLayoutEntry bgl0Entries[] =//
    {
        // World space transform.
        {
            .binding = 0,
            .visibility = wgpu::ShaderStage::Compute,
            .buffer =
            {
                .type = wgpu::BufferBindingType::ReadOnlyStorage,
                .hasDynamicOffset = false,
                .minBindingSize = sizeof(Mat44f),
            },
        },
    };
    wgpu::BindGroupLayoutDescriptor bgl0Desc = //
        {
            .label = "TransformPipelineBg0Layout",
            .entryCount = std::size(bgl0Entries),
            .entries = bgl0Entries,
        };

    // Bind group 1 layout
    wgpu::BindGroupLayoutEntry bgl1Entries[] =//
    {
        // Clip space transform
        {
            .binding = 0,
            .visibility = wgpu::ShaderStage::Compute,
            .buffer =
            {
                .type = wgpu::BufferBindingType::Storage,
                .hasDynamicOffset = false,
                .minBindingSize = sizeof(Mat44f),
            },
        },
    };
    wgpu::BindGroupLayoutDescriptor bgl1Desc = //
        {
            .label = "TransformPipelineBg1Layout",
            .entryCount = std::size(bgl1Entries),
            .entries = bgl1Entries,
        };

    // Bind group 2 layout
    wgpu::BindGroupLayoutEntry bgl2Entries[] =//
    {
        //View/Projection matrix
        {
            .binding = 0,
            .visibility = wgpu::ShaderStage::Compute,
            .buffer =
            {
                .type = wgpu::BufferBindingType::Uniform,
                .hasDynamicOffset = false,
                .minBindingSize = sizeof(Mat44f),
            },
        },
    };

    wgpu::BindGroupLayoutDescriptor bgl2Desc = //
        {
            .label = "TransformPipelineBg2Layout",
            .entryCount = std::size(bgl2Entries),
            .entries = bgl2Entries,
        };

    auto bg0Layout = m_GpuDevice->Device.CreateBindGroupLayout(&bgl0Desc);
    MLG_CHECK(bg0Layout,
        "Failed to create bind group 0 layout for transform pipeline");

    auto bg1Layout = m_GpuDevice->Device.CreateBindGroupLayout(&bgl1Desc);
    MLG_CHECK(bg1Layout,
        "Failed to create bind group 1 layout for transform pipeline");

    auto bg2Layout = m_GpuDevice->Device.CreateBindGroupLayout(&bgl2Desc);
    MLG_CHECK(bg2Layout,
        "Failed to create bind group 2 layout for transform pipeline");

    wgpu::BindGroupLayout bgl[] = //
        {
            bg0Layout,
            bg1Layout,
            bg2Layout
        };

    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "TransformPipelineLayout",
            .bindGroupLayoutCount = std::size(bgl),
            .bindGroupLayouts = bgl,
        };

    auto pipelineLayout =
        m_GpuDevice->Device.CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(pipelineLayout, "Failed to create transform pipeline layout");

    wgpu::ComputePipelineDescriptor pipelineDesc//
    {
        .layout = pipelineLayout,
        .compute//
        {
            .module = m_TransformShader,
            .entryPoint = "main",
        },
    };;

    m_TransformPipeline = m_GpuDevice->Device.CreateComputePipeline(&pipelineDesc);
    MLG_CHECK(m_TransformPipeline, "Failed to create compute pipeline for transform");

    return Result<>::Ok;
}

Result<wgpu::ShaderModule>
DawnRenderer::CreateShader(const char* path)
{
    std::vector<uint8_t> shaderCode;
    auto loadResult = LoadShaderCode(path, shaderCode);
    MLG_CHECK(loadResult);

    wgpu::StringView shaderCodeView{ reinterpret_cast<const char*>(shaderCode.data()),
        shaderCode.size() };
    wgpu::ShaderSourceWGSL wgsl{ { .code = shaderCodeView } };
    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{ .nextInChain = &wgsl };

    wgpu::ShaderModule shaderModule = m_GpuDevice->Device.CreateShaderModule(&shaderModuleDescriptor);
    MLG_CHECK(shaderModule, "Failed to create shader module");

    return shaderModule;
}

Result<>
DawnRenderer::UpdateXformBuffer(wgpu::CommandEncoder cmdEncoder,
    const Mat44f& camera,
    const Mat44f& projection,
    const Model* model)
{
    // Size of the buffer needed to hold the world and projection matrices for all meshes in the
    // current frame.
    const size_t sizeofTransformBuffer = model->GetTransformBuffer()->GetSize();

    if(m_TransformBuffers.NeedsRebuild(sizeofTransformBuffer))
    {
        // Re-allocate buffers.

        m_TransformBuffers.SizeofTransformBuffer = sizeofTransformBuffer;

        auto result = CreateBuffer(m_GpuDevice->Device,
            wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
            sizeofTransformBuffer,
            "ClipSpaceTransformBuffer");
        MLG_CHECK(result);
        m_TransformBuffers.ClipSpaceBuf = *result;

        result = CreateBuffer(m_GpuDevice->Device,
            wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            sizeof(Mat44f),
            "ViewProjTransformBuffer");
        MLG_CHECK(result);
        m_TransformBuffers.ViewProjBuf = *result;

        const wgpu::Buffer transformBuffer =
            static_cast<const DawnGpuStorageBuffer*>(model->GetTransformBuffer())->GetBuffer();

        const wgpu::Buffer meshToTransformMapping =
            static_cast<const DawnGpuStorageBuffer*>(model->GetMeshToTransformMapping())
                ->GetBuffer();

        // Recreate the vertex shader bind group with the new buffer.
        wgpu::BindGroupEntry bg0Entries[] = //
            {
                {
                    .binding = 0,
                    .buffer = transformBuffer,
                    .offset = 0,
                    .size = transformBuffer.GetSize(),
                },
                {
                    .binding = 1,
                    .buffer = meshToTransformMapping,
                    .offset = 0,
                    .size = meshToTransformMapping.GetSize(),
                },
            };

        wgpu::BindGroupDescriptor bg0Desc //
            {
                .label = "ColorPipelineBindGroup0",
                .layout = m_ColorPipeline.Pipeline.GetBindGroupLayout(0),
                .entryCount = std::size(bg0Entries),
                .entries = bg0Entries,
            };

        wgpu::BindGroupEntry bg1Entries[] = //
            {
                {
                    .binding = 0,
                    .buffer = m_TransformBuffers.ClipSpaceBuf,
                    .offset = 0,
                    .size = sizeofTransformBuffer,
                },
            };

        wgpu::BindGroupDescriptor bg1Desc //
            {
                .label = "ColorPipelineBindGroup1",
                .layout = m_ColorPipeline.Pipeline.GetBindGroupLayout(1),
                .entryCount = std::size(bg1Entries),
                .entries = bg1Entries,
            };

        m_ColorPipeline.BindGroup0 = m_GpuDevice->Device.CreateBindGroup(&bg0Desc);
        MLG_CHECK(m_ColorPipeline.BindGroup0, "Failed to create bindgroup 0 for color pipeline");

        m_ColorPipeline.BindGroup1 = m_GpuDevice->Device.CreateBindGroup(&bg1Desc);
        MLG_CHECK(m_ColorPipeline.BindGroup1, "Failed to create bindgroup 1 for color pipeline");

        auto xformBgResult = CreateTransformBindGroups(m_GpuDevice->Device,
            m_TransformPipeline,
            transformBuffer,
            m_TransformBuffers.ClipSpaceBuf,
            m_TransformBuffers.ViewProjBuf,
            m_TransformBuffers.BindGroups);
        MLG_CHECK(xformBgResult, "Failed to create transform bind group");
    }

    // Use inverse of camera transform as view matrix
    const Mat44f viewXform = camera.Inverse();

    // Projection transform
    const Mat44f viewProj = projection.Mul(viewXform);

    m_GpuDevice->Device.GetQueue().WriteBuffer(m_TransformBuffers.ViewProjBuf,
        0,
        viewProj.m,
        sizeof(Mat44f));

    wgpu::ComputePassEncoder pass = cmdEncoder.BeginComputePass();
    pass.SetPipeline(m_TransformPipeline);
    pass.SetBindGroup(0, m_TransformBuffers.BindGroups[0]);
    pass.SetBindGroup(1, m_TransformBuffers.BindGroups[1]);
    pass.SetBindGroup(2, m_TransformBuffers.BindGroups[2]);
    const uint32_t workgroupCountX = static_cast<uint32_t>(model->GetMeshes().size());
    pass.DispatchWorkgroups(workgroupCountX);
    pass.End();

    return Result<>::Ok;
}

Result<GpuTexture*>
DawnRenderer::GetDefaultBaseTexture()
{
    if(!m_DefaultBaseTexture)
    {
        static constexpr const char* MAGENTA_TEXTURE_KEY = "$magenta";

        auto result = m_GpuDevice->CreateTexture("#FF00FFFF"_rgba, imstring(MAGENTA_TEXTURE_KEY));
        MLG_CHECK(result);

        m_DefaultBaseTexture = *result;
    }

    return m_DefaultBaseTexture;
}

static Result<>
CreateTransformBindGroups(wgpu::Device device,
    wgpu::ComputePipeline pipeline,
    wgpu::Buffer worldSpaceBuffer,
    wgpu::Buffer clipSpaceBuffer,
    wgpu::Buffer viewProjBuffer,
    wgpu::BindGroup* bindGroups)
{
    wgpu::BindGroupEntry bg0Entries //
    {
        .binding = 0,
        .buffer = worldSpaceBuffer,
        .offset = 0,
        .size = worldSpaceBuffer.GetSize(),
    };
    wgpu::BindGroupEntry bg1Entries //
    {
        .binding = 0,
        .buffer = clipSpaceBuffer,
        .offset = 0,
        .size = clipSpaceBuffer.GetSize(),
    };
    wgpu::BindGroupEntry bg2Entries //
    {
        .binding = 0,
        .buffer = viewProjBuffer,
        .offset = 0,
        .size = viewProjBuffer.GetSize(),
    };

    wgpu::BindGroupDescriptor bg0Desc//
    {
        .layout = pipeline.GetBindGroupLayout(0),
        .entryCount = 1,
        .entries = &bg0Entries,
    };

    wgpu::BindGroupDescriptor bg1Desc//
    {
        .layout = pipeline.GetBindGroupLayout(1),
        .entryCount = 1,
        .entries = &bg1Entries,
    };

    wgpu::BindGroupDescriptor bg2Desc//
    {
        .layout = pipeline.GetBindGroupLayout(2),
        .entryCount = 1,
        .entries = &bg2Entries,
    };

    bindGroups[0] = device.CreateBindGroup(&bg0Desc);
    MLG_CHECK(bindGroups[0], "Failed to create bind group 0 for transform");
    bindGroups[1] = device.CreateBindGroup(&bg1Desc);
    MLG_CHECK(bindGroups[1], "Failed to create bind group 1 for transform");
    bindGroups[2] = device.CreateBindGroup(&bg2Desc);
    MLG_CHECK(bindGroups[2], "Failed to create bind group 2 for transform");

    return Result<>::Ok;
}

static Result<wgpu::Buffer>
CreateBuffer(wgpu::Device device, wgpu::BufferUsage usage, size_t size, const char* label)
{
    wgpu::BufferDescriptor desc //
    {
        .label = label,
        .usage = usage,
        .size = size,
        .mappedAtCreation = false,
    };

    auto buffer = device.CreateBuffer(&desc);
    MLG_CHECK(buffer, "Failed to create buffer");
    return buffer;
}