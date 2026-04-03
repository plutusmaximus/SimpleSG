#define _CRT_SECURE_NO_WARNINGS

#define __LOGGER_NAME__ "DAWN"

#include "DawnRenderer.h"

#include "Log.h"

#include "Result.h"
#include "scope_exit.h"

#include "DawnGpuDevice.h"
#include "DawnScenePack.h"
#include "PerfMetrics.h"
#include "WebgpuHelper.h"

#include <cstdio>

static constexpr const char* COMPOSITE_COLOR_TARGET_VS = "shaders/FullScreenTriangle.vs.wgsl";
static constexpr const char* COMPOSITE_COLOR_TARGET_FS = "shaders/FullScreenTriangle.fs.wgsl";

static constexpr const char* COLOR_PIPELINE_VS = "shaders/VertexShader.vs.wgsl";
static constexpr const char* COLOR_PIPELINE_FS = "shaders/FragmentShader.fs.wgsl";

static constexpr const char* TRANSFORM_SHADER_CS = "shaders/TransformShader.cs.wgsl";

static Result<wgpu::Buffer>
CreateBuffer(wgpu::Device device, wgpu::BufferUsage usage, size_t size, const char* label);

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
}

template<typename T>
static inline size_t alignUniformBuffer(const wgpu::Limits& limits)
{
    const size_t alignment = limits.minUniformBufferOffsetAlignment;

    return (sizeof(T) + alignment - 1) & ~(alignment - 1);
}

Result<DawnRenderer*>
DawnRenderer::Create(DawnGpuDevice* gpuDevice)
{
    DawnRenderer* renderer = new DawnRenderer(gpuDevice);
    MLG_CHECK(renderer, "Failed to create DawnRenderer");

    auto cleanup = scope_exit([renderer]()
    {
        DawnRenderer::Destroy(renderer);
    });

    MLG_CHECK(renderer->CreateColorAndDepthTargets());
    MLG_CHECK(renderer->CreateColorPipeline());
    MLG_CHECK(renderer->CreateBltPipeline());
    MLG_CHECK(renderer->CreateTransformPipeline());

    cleanup.release();

    return renderer;
}

Result<>
DawnRenderer::Destroy(DawnRenderer* renderer)
{
    delete renderer;
    return Result<>::Ok;
}

Result<>
DawnRenderer::Render(const Mat44f& camera,
    const Mat44f& projection,
    const Model* model,
    RenderCompositor* compositor)
{
    auto transformBuffer = static_cast<const DawnGpuStorageBuffer*>(model->GetTransformBuffer())->GetBuffer();
    auto meshToTransformMappingBuffer = static_cast<const DawnGpuStorageBuffer*>(model->GetMeshToTransformMapping())->GetBuffer();
    auto drawIndirectBuffer = static_cast<const DawnGpuDrawIndirectBuffer*>(model->GetDrawIndirectBuffer())->GetBuffer();
    auto vertexBuffer = static_cast<const DawnGpuVertexBuffer*>(model->GetGpuVertexBuffer())->GetBuffer();
    auto indexBuffer = static_cast<const DawnGpuIndexBuffer*>(model->GetGpuIndexBuffer())->GetBuffer();

    std::vector<MaterialBinding> materialBindings;
    std::vector<uint32_t> meshToMaterialMap;
    materialBindings.reserve(model->GetMeshes().size());
    meshToMaterialMap.reserve(model->GetMeshes().size());

    for(size_t i = 0; i < model->GetMeshes().size(); ++i)
    {
        const Mesh& mesh = model->GetMeshes()[i];
        DawnGpuMaterial* dawnMaterial = static_cast<DawnGpuMaterial*>(mesh.GetGpuMaterial());
        MaterialBinding materialBinding;
        materialBinding.BaseTexture = static_cast<const DawnGpuTexture*>(dawnMaterial->GetBaseTexture())->GetTexture();
        materialBinding.Sampler = static_cast<const DawnGpuTexture*>(dawnMaterial->GetBaseTexture())->GetSampler();
        materialBinding.ConstantsBuffer = dawnMaterial->GetConstantsBuffer();
        materialBinding.BindGroup = dawnMaterial->GetBindGroup();

        materialBindings.emplace_back(std::move(materialBinding));
        meshToMaterialMap.push_back(static_cast<uint32_t>(i));
    }

    wgpu::BindGroup colorRenderBindGroup0;
    wgpu::BindGroup transformBindGroup0;

    {
        // Create the bind group for the color pipeline's vertex shader.
        wgpu::BindGroupEntry bg0Entries[] = //
            {
                // World space transform buffer
                {
                    .binding = 0,
                    .buffer = transformBuffer,
                    .offset = 0,
                    .size = transformBuffer.GetSize(),
                },
                // Mesh-to-transform mapping
                {
                    .binding = 1,
                    .buffer = meshToTransformMappingBuffer,
                    .offset = 0,
                    .size = meshToTransformMappingBuffer.GetSize(),
                },
            };

        wgpu::BindGroupDescriptor bg0Desc //
            {
                .label = "ColorPipelineBindGroup0",
                .layout = m_ColorPipeline.Pipeline.GetBindGroupLayout(0),
                .entryCount = std::size(bg0Entries),
                .entries = bg0Entries,
            };

        colorRenderBindGroup0 = m_GpuDevice->Device.CreateBindGroup(&bg0Desc);
        MLG_CHECK(colorRenderBindGroup0, "Failed to create bindgroup 0 for color pipeline");
    }

    {
        // Create the bind group for the transform compute shader.

        wgpu::BindGroupEntry bg0Entries //
        {
            .binding = 0,
            .buffer = transformBuffer,
            .offset = 0,
            .size = transformBuffer.GetSize(),
        };

        wgpu::BindGroupDescriptor bg0Desc//
        {
            .layout = m_TransformPipeline.GetBindGroupLayout(0),
            .entryCount = 1,
            .entries = &bg0Entries,
        };

        transformBindGroup0 = m_GpuDevice->Device.CreateBindGroup(&bg0Desc);
        MLG_CHECK(transformBindGroup0, "Failed to create bind group 0 for transform");
    }

    DawnScenePack scenePack(indexBuffer,
        vertexBuffer,
        transformBuffer,
        drawIndirectBuffer,
        meshToTransformMappingBuffer,
        colorRenderBindGroup0,
        transformBindGroup0,
        std::move(materialBindings),
        std::move(meshToMaterialMap));

    return Render(camera, projection, scenePack, compositor);
}

Result<>
DawnRenderer::Render(const Mat44f& camera,
    const Mat44f& projection,
    const ScenePack& scenePack,
    RenderCompositor* compositor)
{
    static PerfTimer renderTimer("Renderer.Render");
    auto scopedRenderTimer = renderTimer.StartScoped();

    auto gpuDevice = m_GpuDevice->Device;

    const DawnScenePack& dawnScenePack = static_cast<const DawnScenePack&>(scenePack);

    DawnRenderCompositor* dawnCompositor = static_cast<DawnRenderCompositor*>(compositor);

    wgpu::CommandEncoder cmdEncoder = dawnCompositor->GetCommandEncoder();

    static PerfTimer transformNodesTimer("Renderer.Render.TransformNodes");
    {
        auto scopedTimer = transformNodesTimer.StartScoped();

        auto transformNodesResult = TransformNodes(cmdEncoder, camera, projection, dawnScenePack);
        MLG_CHECK(transformNodesResult);
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
        renderPass.SetBindGroup(0, dawnScenePack.GetColorRenderBindGroup0(), 0, nullptr);
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

        renderPass.SetVertexBuffer(0,
            dawnScenePack.GetVertexBuffer(),
            0,
            dawnScenePack.GetVertexBuffer().GetSize());

        renderPass.SetIndexBuffer(dawnScenePack.GetIndexBuffer(),
            idxFmt,
            0,
            dawnScenePack.GetIndexBuffer().GetSize());
    }

    static PerfTimer drawTimer("Renderer.Render.Draw");
    drawTimer.Start();

    uint64_t indirectOffset = 0;

    const auto& materialBindings = dawnScenePack.GetMaterialBindings();
    const auto& materialIndices = dawnScenePack.GetMaterialIndices();
    const size_t meshCount = dawnScenePack.GetMeshCount();
    const auto& drawIndirectBuffer = dawnScenePack.GetDrawIndirectBuffer();

    for(size_t i = 0; i < meshCount; ++i, indirectOffset += sizeof(DrawIndirectBufferParams))
    {
        static PerfTimer fsBindingTimer("Renderer.Render.Draw.SetMaterialBindGroup");
        {
            auto scopedTimer = fsBindingTimer.StartScoped();

            const uint32_t materialIndex = materialIndices[i];

            renderPass.SetBindGroup(2, materialBindings[materialIndex].BindGroup, 0, nullptr);
        }

        static PerfTimer drawIndexedTimer("Renderer.Render.Draw.DrawIndexed");
        {
            auto scopedTimer = drawIndexedTimer.StartScoped();
            /*renderPass.DrawIndexed(mesh.GetIndexCount(),
                1,
                mesh.GetIndexOffset(),
                mesh.GetVertexOffset(),
                meshCount);*/

            renderPass.DrawIndexedIndirect(drawIndirectBuffer, indirectOffset);
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
            .view = m_ColorTargetView,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { 0.0f, 0.0f, 0.0f, 0.0f },
        };

    static constexpr float CLEAR_DEPTH = 1.0f;

    wgpu::RenderPassDepthStencilAttachment depthStencilAttachment //
        {
            .view = m_DepthTargetView,
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
    static constexpr wgpu::TextureFormat kColorTargetFormat = wgpu::TextureFormat::RGBA8Unorm;
    static constexpr wgpu::TextureFormat kDepthTargetFormat = wgpu::TextureFormat::Depth24Plus;

    const auto screenBounds = m_GpuDevice->GetScreenBounds();

    const unsigned targetWidth = static_cast<unsigned>(screenBounds.Width);
    const unsigned targetHeight = static_cast<unsigned>(screenBounds.Height);

    if(!m_ColorTarget || m_ColorTarget.GetWidth() != targetWidth ||
        m_ColorTarget.GetHeight() != targetHeight)
    {
        MLG_DEBUG("Creating new color target with size {}x{}", targetWidth, targetHeight);

        wgpu::TextureDescriptor textureDesc //
            {
                .label = "ColorTarget",
                .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc |
                         wgpu::TextureUsage::TextureBinding,
                .dimension = wgpu::TextureDimension::e2D,
                .size = //
                {
                    .width = targetWidth,
                    .height = targetHeight,
                    .depthOrArrayLayers = 1,
                },
                .format = kColorTargetFormat,
                .mipLevelCount = 1,
                .sampleCount = 1,
            };

        m_ColorTarget = m_GpuDevice->Device.CreateTexture(&textureDesc);
        m_ColorTargetView = m_ColorTarget.CreateView();

        wgpu::SamplerDescriptor samplerDesc //
            {
                .label = "ColorTargetSampler",
                .addressModeU = wgpu::AddressMode::Repeat,
                .addressModeV = wgpu::AddressMode::Repeat,
                .addressModeW = wgpu::AddressMode::Undefined,
                .magFilter = wgpu::FilterMode::Linear,
                .minFilter = wgpu::FilterMode::Linear,
                .mipmapFilter = wgpu::MipmapFilterMode::Undefined,
                .lodMinClamp = 0.0f,
                .lodMaxClamp = 32.0f,
                .compare = wgpu::CompareFunction::Undefined,
                .maxAnisotropy = 1,
            };

        m_ColorTargetSampler = m_GpuDevice->Device.CreateSampler(&samplerDesc);
    }

    if(!m_DepthTarget || m_DepthTarget.GetWidth() != targetWidth ||
        m_DepthTarget.GetHeight() != targetHeight)
    {
        MLG_DEBUG("Creating new depth target with size {}x{}", targetWidth, targetHeight);

        wgpu::TextureDescriptor textureDesc //
            {
                .label = "DepthTarget",
                .usage = wgpu::TextureUsage::RenderAttachment,
                .dimension = wgpu::TextureDimension::e2D,
                .size = //
                {
                    .width = targetWidth,
                    .height = targetHeight,
                    .depthOrArrayLayers = 1,
                },
                .format = kDepthTargetFormat,
                .mipLevelCount = 1,
                .sampleCount = 1,
            };

        m_DepthTarget = m_GpuDevice->Device.CreateTexture(&textureDesc);
        m_DepthTargetView = m_DepthTarget.CreateView();
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

    // Color target pipeline layout

    auto layouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(layouts);

    wgpu::BindGroupLayout colorTargetBgl[] = //
        {
            layouts->Bindgroup0Layout,
            layouts->Bindgroup1Layout,
            layouts->Bindgroup2Layout,
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
            .format = m_ColorTarget.GetFormat(),
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    wgpu::DepthStencilState depthStencilState //
        {
            .format = m_DepthTarget.GetFormat(),
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

    // BLT pipeline bind group layout

    auto layouts = WebgpuHelper::GetCompositorPipelineLayouts();
    MLG_CHECK(layouts);

    wgpu::BindGroupLayout bltBgl[] = //
        {
            nullptr,    //bind group 0
            nullptr,    //bind group 1
            layouts->Bindgroup2Layout,
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

    wgpu::BindGroupEntry bgEntries[] = //
        {
            {
                .binding = 0,
                .textureView = m_ColorTargetView,
            },
            {
                .binding = 1,
                .sampler = m_ColorTargetSampler,
            },
        };

    wgpu::BindGroupDescriptor bgDesc //
        {
            .label = "ColorTargetCopyBindGroup",
            .layout = layouts->Bindgroup2Layout,
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

    auto layouts = WebgpuHelper::GetTransformPipelineLayouts();
    MLG_CHECK(layouts);

    wgpu::BindGroupLayout bgl[] = //
        {
            layouts->Bindgroup0Layout,
            layouts->Bindgroup1Layout,
            layouts->Bindgroup2Layout
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
DawnRenderer::TransformNodes(wgpu::CommandEncoder cmdEncoder,
    const Mat44f& camera,
    const Mat44f& projection,
    const ScenePack& scenePack)
{
    const DawnScenePack& dawnScenePack = static_cast<const DawnScenePack&>(scenePack);

    // Reallocate buffers if needed.

    if(!m_TransformBuffers.ClipSpaceBuf || !m_TransformBuffers.ViewProjBuf ||
        !m_TransformBuffers.BindGroup1 || !m_TransformBuffers.BindGroup2 ||
        scenePack.GetTransformCount() > m_TransformBuffers.TransformCount)
    {
        m_TransformBuffers.TransformCount = scenePack.GetTransformCount();

        auto result = CreateBuffer(m_GpuDevice->Device,
            wgpu::BufferUsage::Storage | wgpu::BufferUsage::CopyDst,
            dawnScenePack.GetTransformBuffer().GetSize(),
            "ClipSpaceTransformBuffer");
        MLG_CHECK(result);
        m_TransformBuffers.ClipSpaceBuf = *result;

        result = CreateBuffer(m_GpuDevice->Device,
            wgpu::BufferUsage::Uniform | wgpu::BufferUsage::CopyDst,
            sizeof(Mat44f),
            "ViewProjTransformBuffer");
        MLG_CHECK(result);
        m_TransformBuffers.ViewProjBuf = *result;

        // Color pipeline bind groups
        {
            // Bind group 1
            wgpu::BindGroupEntry bg1Entries[] = //
                {
                    // Clip space transform buffer
                    {
                        .binding = 0,
                        .buffer = m_TransformBuffers.ClipSpaceBuf,
                        .offset = 0,
                        .size = m_TransformBuffers.ClipSpaceBuf.GetSize(),
                    },
                };

            wgpu::BindGroupDescriptor bg1Desc //
                {
                    .label = "ColorPipelineBindGroup1",
                    .layout = m_ColorPipeline.Pipeline.GetBindGroupLayout(1),
                    .entryCount = std::size(bg1Entries),
                    .entries = bg1Entries,
                };

            m_ColorPipeline.BindGroup1 = m_GpuDevice->Device.CreateBindGroup(&bg1Desc);
            MLG_CHECK(m_ColorPipeline.BindGroup1,
                "Failed to create bindgroup 1 for color pipeline");
        }

        // Transform pipeline bind groups
        {
            wgpu::BindGroupEntry bg1Entries //
            {
                .binding = 0,
                .buffer = m_TransformBuffers.ClipSpaceBuf,
                .offset = 0,
                .size = m_TransformBuffers.ClipSpaceBuf.GetSize(),
            };

            wgpu::BindGroupDescriptor bg1Desc//
            {
                .layout = m_TransformPipeline.GetBindGroupLayout(1),
                .entryCount = 1,
                .entries = &bg1Entries,
            };

            m_TransformBuffers.BindGroup1 = m_GpuDevice->Device.CreateBindGroup(&bg1Desc);
            MLG_CHECK(m_TransformBuffers.BindGroup1, "Failed to create bind group 1 for transform");

            wgpu::BindGroupEntry bg2Entries //
            {
                .binding = 0,
                .buffer = m_TransformBuffers.ViewProjBuf,
                .offset = 0,
                .size = m_TransformBuffers.ViewProjBuf.GetSize(),
            };

            wgpu::BindGroupDescriptor bg2Desc//
            {
                .layout = m_TransformPipeline.GetBindGroupLayout(2),
                .entryCount = 1,
                .entries = &bg2Entries,
            };

            m_TransformBuffers.BindGroup2 = m_GpuDevice->Device.CreateBindGroup(&bg2Desc);
            MLG_CHECK(m_TransformBuffers.BindGroup2, "Failed to create bind group 2 for transform");
        }
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
    pass.SetBindGroup(0, dawnScenePack.GetTransformBindGroup0());
    pass.SetBindGroup(1, m_TransformBuffers.BindGroup1);
    pass.SetBindGroup(2, m_TransformBuffers.BindGroup2);
    const uint32_t workgroupCountX = dawnScenePack.GetTransformCount();
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