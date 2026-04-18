#define _CRT_SECURE_NO_WARNINGS

#define __LOGGER_NAME__ "DAWN"

#include "DawnRenderer.h"

#include "DawnRenderCompositor.h"
#include "DawnSceneKit.h"
#include "Log.h"
#include "PerfMetrics.h"
#include "Projection.h"
#include "Result.h"
#include "scope_exit.h"
#include "ShaderTypes.h"
#include "WebgpuHelper.h"

#include <cstdio>
#include <SDL3/SDL.h>

static constexpr const char* COMPOSITOR_SHADER = "shaders/ResolveShader.wgsl";

static constexpr const char* COLOR_SHADER = "shaders/ColorShader.wgsl";

static constexpr const char* TRANSFORM_SHADER = "shaders/TransformShader.wgsl";

Result<>
DawnRenderer::Startup()
{
    MLG_CHECKV(!m_Initialized, "DawnRenderer is already initialized");

    WebgpuHelper::GetDevice().GetLimits(&m_GpuLimits);

    MLG_CHECK(CreateColorAndDepthTargets());
    MLG_CHECK(CreateColorPipeline());
    MLG_CHECK(CreateResolvePipeline());
    MLG_CHECK(CreateTransformPipeline());

    m_Initialized = true;

    return Result<>::Ok;
}

Result<>
DawnRenderer::Shutdown()
{
    if(!m_Initialized)
    {
        // Not initialized, nothing to do
        return Result<>::Ok;
    }

    m_ColorPipeline = {};
    m_ResolvePipeline = {};
    m_TransformPipeline = {};
    m_TransformBuffers = {};

    m_ColorTargetSampler = nullptr;
    m_ColorTargetView = nullptr;
    m_ColorTarget = nullptr;
    m_DepthTargetView = nullptr;
    m_DepthTarget = nullptr;

    m_Initialized = false;

    return Result<>::Ok;
}

Result<>
DawnRenderer::Render(const Mat44f& camera,
    const Projection& projection,
    const SceneKit& sceneKit,
    DawnRenderCompositor& compositor)
{
    MLG_CHECKV(m_Initialized, "DawnRenderer is not initialized");

    static PerfTimer renderTimer("Renderer.Render");
    auto scopedRenderTimer = renderTimer.StartScoped();

    const DawnSceneKit& dawnSceneKit = static_cast<const DawnSceneKit&>(sceneKit);

    wgpu::CommandEncoder cmdEncoder = compositor.GetCommandEncoder();

    static PerfTimer transformNodesTimer("Renderer.Render.TransformNodes");
    {
        auto scopedTimer = transformNodesTimer.StartScoped();

        auto transformNodesResult = TransformNodes(cmdEncoder, camera, projection, dawnSceneKit);
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
        renderPass.SetBindGroup(0, dawnSceneKit.GetColorPipelineBindGroup0(), 0, nullptr);
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
            dawnSceneKit.GetVertexBuffer().GetGpuBuffer(),
            0,
            dawnSceneKit.GetVertexBuffer().GetSize());

        renderPass.SetIndexBuffer(dawnSceneKit.GetIndexBuffer().GetGpuBuffer(),
            idxFmt,
            0,
            dawnSceneKit.GetIndexBuffer().GetSize());
    }

    static PerfTimer drawTimer("Renderer.Render.Draw");
    drawTimer.Start();

    uint64_t indirectOffset = 0;

    const auto& materialBindGroups = dawnSceneKit.GetMaterialBindGroups();
    const auto& meshes = dawnSceneKit.GetMeshes();
    const auto& modelInstances = dawnSceneKit.GetModelInstances();
    const auto& drawIndirectBuffer = dawnSceneKit.GetDrawIndirectBuffer();

    uint32_t lastMaterialIndex = UINT32_MAX;

    for(const auto& modelInstance : modelInstances)
    {
        std::span<const MeshProperties> instanceMeshes(
            meshes.data() + modelInstance.FirstMesh, modelInstance.MeshCount);

        for(const auto& mesh : instanceMeshes)
        {
            const uint32_t materialIndex = mesh.MaterialIndex;

            if(materialIndex != lastMaterialIndex)
            {
                static PerfTimer fsBindingTimer("Renderer.Render.Draw.SetMaterialBindGroup");
                {
                    auto scopedTimer = fsBindingTimer.StartScoped();

                    renderPass.SetBindGroup(2, materialBindGroups[materialIndex], 0, nullptr);
                    lastMaterialIndex = materialIndex;
                }
            }

            static PerfTimer drawIndexedTimer("Renderer.Render.Draw.DrawIndexed");
            {
                auto scopedTimer = drawIndexedTimer.StartScoped();

                renderPass.DrawIndexedIndirect(drawIndirectBuffer.GetGpuBuffer(), indirectOffset);
                indirectOffset += sizeof(ShaderTypes::DrawIndirectBufferParams);
            }
        }
    }

    drawTimer.Stop();

    renderPass.End();

    static PerfTimer resolveTimer("Renderer.Render.Resolve");
    resolveTimer.Start();

    static PerfTimer resolveColorTargetTimer("Renderer.Render.Resolve.ResolveColorTarget");
    {
        auto scopedTimer = resolveColorTargetTimer.StartScoped();
        auto resolveResult = ResolveColorTargetToSwapchain(compositor);
        MLG_CHECK(resolveResult);
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
DawnRenderer::ResolveColorTargetToSwapchain(DawnRenderCompositor& compositor)
{
    wgpu::Texture target = compositor.GetTarget();

    if(!target)
    {
        // Off-screen rendering, skip rendering to swapchain
        return Result<>::Ok;
    }

    wgpu::RenderPassColorAttachment attachment //
        {
            .view = target.CreateView(),
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

    wgpu::RenderPassEncoder renderPass = compositor.GetCommandEncoder().BeginRenderPass(&renderPassDesc);
    MLG_CHECK(renderPass, "Failed to begin render pass for copying color target to swapchain");

    renderPass.SetPipeline(m_ResolvePipeline.Pipeline);
    renderPass.SetBindGroup(2, m_ResolvePipeline.BindGroup2, 0, nullptr);
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
    static constexpr wgpu::TextureFormat kDepthTargetFormat = wgpu::TextureFormat::Depth24Plus;

    const auto screenBounds = WebgpuHelper::GetScreenBounds();

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
                .format = WebgpuHelper::GetSwapChainFormat(),
                .mipLevelCount = 1,
                .sampleCount = 1,
            };

        m_ColorTarget = WebgpuHelper::GetDevice().CreateTexture(&textureDesc);
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

        m_ColorTargetSampler = WebgpuHelper::GetDevice().CreateSampler(&samplerDesc);
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

        m_DepthTarget = WebgpuHelper::GetDevice().CreateTexture(&textureDesc);
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

    auto shader = CreateShader(COLOR_SHADER);
    MLG_CHECK(shader);

    m_ColorPipeline.Shader = *shader;

    // Color target pipeline layout

    auto bgLayouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(bgLayouts);

    wgpu::BindGroupLayout colorTargetBgl[] = //
        {
            (*bgLayouts)[0],
            (*bgLayouts)[1],
            (*bgLayouts)[2],
        };

    wgpu::PipelineLayoutDescriptor colorTargetPipelineLayoutDesc //
        {
            .label = "ColorPipelineLayout",
            .bindGroupLayoutCount = std::size(colorTargetBgl),
            .bindGroupLayouts = colorTargetBgl,
        };

    m_ColorPipeline.Layout =
        WebgpuHelper::GetDevice().CreatePipelineLayout(&colorTargetPipelineLayoutDesc);
    MLG_CHECK(m_ColorPipeline.Layout, "Failed to create color pipeline layout");

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
            .module = m_ColorPipeline.Shader,
            .entryPoint = "fs_main",
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
        .layout = m_ColorPipeline.Layout,
        .vertex =
        {
            .module = m_ColorPipeline.Shader,
            .entryPoint = "vs_main",
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

    m_ColorPipeline.Pipeline = WebgpuHelper::GetDevice().CreateRenderPipeline(&descriptor);
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
DawnRenderer::CreateResolvePipeline()
{
    if(m_ResolvePipeline.Pipeline)
    {
        return Result<>::Ok;
    }

    auto shader = CreateShader(COMPOSITOR_SHADER);
    MLG_CHECK(shader);

    m_ResolvePipeline.Shader = *shader;

    // Resolve pipeline bind group layout

    auto bgLayouts = WebgpuHelper::GetCompositorPipelineLayouts();
    MLG_CHECK(bgLayouts);

    wgpu::BindGroupLayout resolveBgl[] = //
        {
            nullptr,    //bind group 0
            nullptr,    //bind group 1
            (*bgLayouts)[2],
        };

    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "ResolvePipelineLayout",
            .bindGroupLayoutCount = std::size(resolveBgl),
            .bindGroupLayouts = resolveBgl,
        };

    m_ResolvePipeline.Layout = WebgpuHelper::GetDevice().CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(m_ResolvePipeline.Layout, "Failed to create resolve pipeline layout");

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
            .format = WebgpuHelper::GetSwapChainFormat(),
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    wgpu::FragmentState fragmentState //
        {
            .module = m_ResolvePipeline.Shader,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTargetState,
        };

    wgpu::RenderPipelineDescriptor descriptor//
    {
        .label = "CopyColorTargetPipeline",
        .layout = m_ResolvePipeline.Layout,
        .vertex =
        {
            .module = m_ResolvePipeline.Shader,
            .entryPoint = "vs_main",
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
            .layout = (*bgLayouts)[2],
            .entryCount = std::size(bgEntries),
            .entries = bgEntries,
        };

    m_ResolvePipeline.BindGroup2 = WebgpuHelper::GetDevice().CreateBindGroup(&bgDesc);
    MLG_CHECK(m_ResolvePipeline.BindGroup2, "Failed to create bind group 2 for resolve pipeline");

    m_ResolvePipeline.Pipeline = WebgpuHelper::GetDevice().CreateRenderPipeline(&descriptor);
    MLG_CHECK(m_ResolvePipeline.Pipeline, "Failed to create render pipeline for resolve pipeline");

    return Result<>::Ok;
}

Result<>
DawnRenderer::CreateTransformPipeline()
{
    if(m_TransformPipeline)
    {
        return Result<>::Ok;
    }

    auto csResult = CreateShader(TRANSFORM_SHADER);
    MLG_CHECK(csResult);

    m_TransformShader = *csResult;

    auto bgLayouts = WebgpuHelper::GetTransformPipelineLayouts();
    MLG_CHECK(bgLayouts);

    wgpu::BindGroupLayout bgl[] = //
        {
            (*bgLayouts)[0],
            (*bgLayouts)[1],
            (*bgLayouts)[2]
        };

    wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "TransformPipelineLayout",
            .bindGroupLayoutCount = std::size(bgl),
            .bindGroupLayouts = bgl,
        };

    wgpu::PipelineLayout pipelineLayout =
        WebgpuHelper::GetDevice().CreatePipelineLayout(&pipelineLayoutDesc);
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

    m_TransformPipeline = WebgpuHelper::GetDevice().CreateComputePipeline(&pipelineDesc);
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
    wgpu::ShaderModuleDescriptor shaderModuleDescriptor{ .nextInChain = &wgsl, .label = path };

    wgpu::ShaderModule shaderModule =
        WebgpuHelper::GetDevice().CreateShaderModule(&shaderModuleDescriptor);
    MLG_CHECK(shaderModule, "Failed to create shader module");

    return shaderModule;
}

Result<>
DawnRenderer::TransformNodes(wgpu::CommandEncoder cmdEncoder,
    const Mat44f& camera,
    const Projection& projection,
    const SceneKit& sceneKit)
{
    const DawnSceneKit& dawnSceneKit = static_cast<const DawnSceneKit&>(sceneKit);

    wgpu::Device device = WebgpuHelper::GetDevice();

    // Reallocate buffers if needed.

    if(!m_TransformBuffers.ClipSpaceBuf || !m_TransformBuffers.CameraParamsBuf ||
        !m_TransformBuffers.BindGroup1 || !m_TransformBuffers.BindGroup2 ||
        dawnSceneKit.GetTransformCount() > m_TransformBuffers.TransformCount)
    {
        m_TransformBuffers.TransformCount = dawnSceneKit.GetTransformCount();

        auto clipSpaceBuffer =
            WebgpuHelper::CreateTypedStorageBuffer<ClipSpaceBuffer>(dawnSceneKit.GetTransformBuffer().GetSize(),
                "ClipSpaceTransformBuffer");
        MLG_CHECK(clipSpaceBuffer);

        m_TransformBuffers.ClipSpaceBuf = *clipSpaceBuffer;

        auto cameraParamsBuf = WebgpuHelper::CreateTypedUniformBuffer<CameraParamsBuffer>(
            sizeof(ShaderTypes::CameraParams),
            "CameraParamsBuffer");
        MLG_CHECK(cameraParamsBuf);
        m_TransformBuffers.CameraParamsBuf = *cameraParamsBuf;

        // Color pipeline bind groups
        {
            // Bind group 1
            wgpu::BindGroupEntry bg1Entries[] = //
                {
                    // Clip space transform buffer
                    {
                        .binding = 0,
                        .buffer = m_TransformBuffers.ClipSpaceBuf.GetGpuBuffer(),
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

            m_ColorPipeline.BindGroup1 = device.CreateBindGroup(&bg1Desc);
            MLG_CHECK(m_ColorPipeline.BindGroup1,
                "Failed to create bindgroup 1 for color pipeline");
        }

        // Transform pipeline bind groups
        {
            wgpu::BindGroupEntry bg1Entries //
            {
                .binding = 0,
                .buffer = m_TransformBuffers.ClipSpaceBuf.GetGpuBuffer(),
                .offset = 0,
                .size = m_TransformBuffers.ClipSpaceBuf.GetSize(),
            };

            wgpu::BindGroupDescriptor bg1Desc//
            {
                .layout = m_TransformPipeline.GetBindGroupLayout(1),
                .entryCount = 1,
                .entries = &bg1Entries,
            };

            m_TransformBuffers.BindGroup1 = device.CreateBindGroup(&bg1Desc);
            MLG_CHECK(m_TransformBuffers.BindGroup1, "Failed to create bind group 1 for transform");

            wgpu::BindGroupEntry bg2Entries //
            {
                .binding = 0,
                .buffer = m_TransformBuffers.CameraParamsBuf.GetGpuBuffer(),
                .offset = 0,
                .size = m_TransformBuffers.CameraParamsBuf.GetSize(),
            };

            wgpu::BindGroupDescriptor bg2Desc//
            {
                .layout = m_TransformPipeline.GetBindGroupLayout(2),
                .entryCount = 1,
                .entries = &bg2Entries,
            };

            m_TransformBuffers.BindGroup2 = device.CreateBindGroup(&bg2Desc);
            MLG_CHECK(m_TransformBuffers.BindGroup2, "Failed to create bind group 2 for transform");
        }
    }

    // Use inverse of camera transform as view matrix
    const Mat44f viewXform = camera.Inverse();
    const Mat44f& projMat = projection.GetMatrix();

    // Projection transform
    const Mat44f viewProj = projMat.Mul(viewXform);

    const ShaderTypes::CameraParams cameraParams //
        {
            .View = viewXform,
            .Projection = projMat,
            .ViewProj = viewProj,
        };

    device.GetQueue().WriteBuffer(
        m_TransformBuffers.CameraParamsBuf.GetGpuBuffer(),
        0,
        &cameraParams,
        sizeof(ShaderTypes::CameraParams));

    wgpu::ComputePassEncoder pass = cmdEncoder.BeginComputePass();
    pass.SetPipeline(m_TransformPipeline);
    pass.SetBindGroup(0, dawnSceneKit.GetTransformPipelineBindGroup0());
    pass.SetBindGroup(1, m_TransformBuffers.BindGroup1);
    pass.SetBindGroup(2, m_TransformBuffers.BindGroup2);
    const uint32_t workgroupCountX = dawnSceneKit.GetTransformCount();
    pass.DispatchWorkgroups(workgroupCountX);
    pass.End();

    return Result<>::Ok;
}