#define MLG_LOGGER_NAME "DAWN"

#include "Renderer.h"

#include "Compositor.h"
#include "FileFetcher.h"
#include "PropKit.h"
#include "narrow_cast.h"
#include "PerfMetrics.h"
#include "Projection.h"
#include "Scene.h"
#include "scope_exit.h"
#include "ShaderInterop.h"
#include "WebgpuHelper.h"

#include <SDL3/SDL.h>
#include <thread>

static constexpr const char* kPresentShader = "shaders/PresentShader.wgsl";

static constexpr const char* kColorShader = "shaders/ColorShader.wgsl";

static constexpr const char* kTransformShader = "shaders/TransformShader.wgsl";

namespace
{
Result<>
LoadShaderCode(const char* filePath, std::vector<uint8_t>& outBuffer)
{
    FileFetcher::Request request(filePath);
    MLG_CHECK(FileFetcher::Fetch(request));

    while(request.IsPending())
    {
        MLG_CHECK(FileFetcher::ProcessCompletions());
        std::this_thread::yield();
    }

    MLG_CHECK(request.Succeeded(), "Failed to load shader file: {}", filePath);

    request.MoveDataTo(outBuffer);

    return Result<>::Ok;
}    

Result<wgpu::ShaderModule>
CreateShader(const char* path)
{
    std::vector<uint8_t> shaderCode;
    auto loadResult = LoadShaderCode(path, shaderCode);
    MLG_CHECK(loadResult);

    const void* data = shaderCode.data();
    const wgpu::StringView shaderCodeView{ static_cast<const char*>(data), shaderCode.size() };
    const wgpu::ShaderSourceWGSL wgsl{ { .nextInChain = nullptr, .code = shaderCodeView } };
    const wgpu::ShaderModuleDescriptor shaderModuleDescriptor{ .nextInChain = &wgsl, .label = path };

    wgpu::ShaderModule shaderModule =
        WebgpuHelper::GetDevice().CreateShaderModule(&shaderModuleDescriptor);
    MLG_CHECK(shaderModule, "Failed to create shader module");

    return shaderModule;
}
}

Result<>
Renderer::Startup()
{
    MLG_CHECKV(!m_Initialized, "Renderer is already initialized");

    WebgpuHelper::GetDevice().GetLimits(&m_GpuLimits);

    MLG_CHECK(CreateColorAndDepthTargets());
    MLG_CHECK(CreateColorPipeline());
    MLG_CHECK(CreatePresentPipeline());
    MLG_CHECK(CreateTransformPipeline());

    m_Initialized = true;

    return Result<>::Ok;
}

Result<>
Renderer::Shutdown()
{
    if(!m_Initialized)
    {
        // Not initialized, nothing to do
        return Result<>::Ok;
    }

    m_ColorPipeline = {};
    m_TransformPipeline = {};
    m_PresentPipeline = {};

    m_ColorTargetSampler = nullptr;
    m_ColorTargetView = nullptr;
    m_ColorTarget = nullptr;
    m_DepthTargetView = nullptr;
    m_DepthTarget = nullptr;

    m_Initialized = false;

    return Result<>::Ok;
}

Result<>
Renderer::Render(const TrsTransformf& camera,
    const Projection& projection,
    const Scene& scene,
    const PropKit& propKit,
    Compositor& compositor)
{
    MLG_CHECKV(m_Initialized, "Renderer is not initialized");

    MLG_SCOPED_TIMER("Renderer.Render");

    const wgpu::CommandEncoder cmdEncoder = compositor.GetCommandEncoder();

    {
        MLG_SCOPED_TIMER("Renderer.Render.TransformNodes");

        auto transformNodesResult = TransformNodes(cmdEncoder, camera, projection, scene);
        MLG_CHECK(transformNodesResult);
    }

    wgpu::RenderPassEncoder renderPass;
    {
        MLG_SCOPED_TIMER("Renderer.Render.BeginRenderPass");
        auto renderPassResult = BeginRenderPass(cmdEncoder);
        MLG_CHECK(renderPassResult);

        renderPass = *renderPassResult;
    }

    {
        MLG_SCOPED_TIMER("Renderer.Render.SetPipeline");

        renderPass.SetPipeline(m_ColorPipeline.Pipeline);
    }

    {
        MLG_SCOPED_TIMER("Renderer.Render.Draw.SetPerFrameBindGroup");
        renderPass.SetBindGroup(0, scene.GetColorPipelineBindGroup0(), 0, nullptr);
    }

    {
        MLG_SCOPED_TIMER("Renderer.Render.Draw.SetBuffers");

        static_assert(VERTEX_INDEX_BITS == UINT32_WIDTH || VERTEX_INDEX_BITS == UINT16_WIDTH);

        constexpr wgpu::IndexFormat idxFmt =
            (VERTEX_INDEX_BITS == UINT32_WIDTH)
            ? wgpu::IndexFormat::Uint32
            : wgpu::IndexFormat::Uint16;

        renderPass.SetVertexBuffer(0,
            propKit.GetVertexBuffer().GetGpuBuffer(),
            0,
            propKit.GetVertexBuffer().BufferSize());

        renderPass.SetIndexBuffer(propKit.GetIndexBuffer().GetGpuBuffer(),
            idxFmt,
            0,
            propKit.GetIndexBuffer().BufferSize());
    }

    static PerfTimer drawTimer("Renderer.Render.Draw");
    drawTimer.Start();

    uint64_t indirectOffset = 0;

    const auto& materialBindGroups = propKit.GetMaterialBindGroups();
    const auto& meshes = propKit.GetMeshes();
    const auto& models = propKit.GetModels();
    const auto& modelInstances = scene.GetModelInstances();
    const auto& drawIndirectBuffer = scene.GetDrawIndirectBuffer();

    MaterialIndex lastMaterialIndex = MaterialIndex::INVALID;

    for(const auto& modelInstance : modelInstances)
    {
        const Model& model = models[modelInstance.GetModelIndex().Value()];

        if(!modelInstance.IsVisible())
        {
            indirectOffset += model.MeshCount * sizeof(ShaderInterop::DrawIndirectParams);
            continue;
        }

        for(uint32_t i = 0; i < model.MeshCount; ++i)
        {
            const Mesh& mesh = meshes[model.FirstMesh.Value() + i];

            const MaterialIndex materialIndex = mesh.MaterialIndex;

            if(materialIndex != lastMaterialIndex)
            {
                MLG_SCOPED_TIMER("Renderer.Render.Draw.SetMaterialBindGroup");

                renderPass.SetBindGroup(1, materialBindGroups[materialIndex.Value()], 0, nullptr);
                lastMaterialIndex = materialIndex;
            }

            {
                MLG_SCOPED_TIMER("Renderer.Render.Draw.DrawIndexed");

                renderPass.DrawIndexedIndirect(drawIndirectBuffer.GetGpuBuffer(), indirectOffset);
                indirectOffset += sizeof(ShaderInterop::DrawIndirectParams);
            }
        }
    }

    drawTimer.Stop();

    renderPass.End();

    {
        MLG_SCOPED_TIMER("Renderer.Render.Present");
        auto presentResult = Present(compositor);
        MLG_CHECK(presentResult);
    }

    return Result<>::Ok;
}

//private:

Result<wgpu::RenderPassEncoder>
Renderer::BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder)
{
    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = m_ColorTargetView,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 0.0f },
        };

    static constexpr float CLEAR_DEPTH = 1.0f;

    const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment //
        {
            .view = m_DepthTargetView,
            .depthLoadOp = wgpu::LoadOp::Clear,
            .depthStoreOp = wgpu::StoreOp::Store,
            .depthClearValue = CLEAR_DEPTH,
            .stencilLoadOp = wgpu::LoadOp::Undefined,
            .stencilStoreOp = wgpu::StoreOp::Undefined,
            .stencilClearValue = 0,
        };

    const wgpu::RenderPassDescriptor renderPassDesc //
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
Renderer::Present(Compositor& compositor) const
{
    const wgpu::Texture target = compositor.GetTarget();

    if(!target)
    {
        // Off-screen rendering, skip rendering to swapchain
        return Result<>::Ok;
    }

    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = target.CreateView(),
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
        };

    const wgpu::RenderPassDescriptor renderPassDesc //
        {
            .label = "CopyRenderPass",
            .colorAttachmentCount = 1,
            .colorAttachments = &attachment,
        };

    const wgpu::RenderPassEncoder renderPass = compositor.GetCommandEncoder().BeginRenderPass(&renderPassDesc);
    MLG_CHECK(renderPass, "Failed to begin render pass for copying color target to swapchain");

    renderPass.SetPipeline(m_PresentPipeline.Pipeline);
    renderPass.SetBindGroup(0, m_PresentPipeline.BindGroup0, 0, nullptr);
    renderPass.Draw(3, 1, 0, 0);
    renderPass.End();

    return Result<>::Ok;
}

Result<>
Renderer::CreateColorAndDepthTargets()
{
    static constexpr wgpu::TextureFormat kDepthTargetFormat = wgpu::TextureFormat::Depth24Plus;

    const auto screenBounds = WebgpuHelper::GetScreenBounds();

    const unsigned targetWidth = static_cast<unsigned>(screenBounds.width);
    const unsigned targetHeight = static_cast<unsigned>(screenBounds.height);

    if(!m_ColorTarget || m_ColorTarget.GetWidth() != targetWidth ||
        m_ColorTarget.GetHeight() != targetHeight)
    {
        MLG_DEBUG("Creating new color target with size {}x{}", targetWidth, targetHeight);

        const wgpu::TextureDescriptor textureDesc //
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

        const wgpu::SamplerDescriptor samplerDesc //
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

        const wgpu::TextureDescriptor textureDesc //
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
Renderer::CreateColorPipeline()
{
    if(m_ColorPipeline.Pipeline)
    {
        return Result<>::Ok;
    }

    MLG_CHECKV(m_ColorTarget, "Color target is null");

    auto shader = CreateShader(kColorShader);
    MLG_CHECK(shader);

    m_ColorPipeline.Shader = *shader;

    // Color target pipeline layout

    auto bgLayouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(bgLayouts);

    const wgpu::PipelineLayoutDescriptor colorTargetPipelineLayoutDesc //
        {
            .label = "ColorPipelineLayout",
            .bindGroupLayoutCount = std::size(*bgLayouts),
            .bindGroupLayouts = bgLayouts->data(),
        };

    m_ColorPipeline.Layout =
        WebgpuHelper::GetDevice().CreatePipelineLayout(&colorTargetPipelineLayoutDesc);
    MLG_CHECK(m_ColorPipeline.Layout, "Failed to create color pipeline layout");

    const wgpu::BlendState blendState //
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

    const wgpu::ColorTargetState colorTargetState //
        {
            .format = m_ColorTarget.GetFormat(),
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    const wgpu::DepthStencilState depthStencilState //
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

    const wgpu::FragmentState fragmentState //
        {
            .module = m_ColorPipeline.Shader,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTargetState,
        };

    const wgpu::VertexAttribute vertexAttributes[] //
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
    const wgpu::VertexBufferLayout vertexBufferLayout //
        {
            .stepMode = wgpu::VertexStepMode::Vertex,
            .arrayStride = sizeof(Vertex),
            .attributeCount = std::size(vertexAttributes),
            .attributes = &vertexAttributes[0],
        };

    const wgpu::RenderPipelineDescriptor descriptor//
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
Renderer::CreatePresentPipeline()
{
    if(m_PresentPipeline.Pipeline)
    {
        return Result<>::Ok;
    }

    const wgpu::Device device = WebgpuHelper::GetDevice();

    auto shader = CreateShader(kPresentShader);
    MLG_CHECK(shader);

    m_PresentPipeline.Shader = *shader;

    // Present pipeline bind group layout

    auto bgLayouts = WebgpuHelper::GetCompositorPipelineLayouts();
    MLG_CHECK(bgLayouts);

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "PresentPipelineLayout",
            .bindGroupLayoutCount = std::size(*bgLayouts),
            .bindGroupLayouts = bgLayouts->data(),
        };

    m_PresentPipeline.Layout = device.CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(m_PresentPipeline.Layout, "Failed to create present pipeline layout");

    const wgpu::BlendState blendState //
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

    const wgpu::ColorTargetState colorTargetState //
        {
            .format = WebgpuHelper::GetSwapChainFormat(),
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    const wgpu::FragmentState fragmentState //
        {
            .module = m_PresentPipeline.Shader,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTargetState,
        };

    const wgpu::RenderPipelineDescriptor descriptor//
    {
        .label = "CopyColorTargetPipeline",
        .layout = m_PresentPipeline.Layout,
        .vertex =
        {
            .module = m_PresentPipeline.Shader,
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

    const wgpu::BindGroupEntry bgEntries[] = //
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

    const wgpu::BindGroupDescriptor bgDesc //
        {
            .label = "ColorTargetCopyBindGroup",
            .layout = (*bgLayouts)[0],
            .entryCount = std::size(bgEntries),
            .entries = &bgEntries[0],
        };

    m_PresentPipeline.BindGroup0 = device.CreateBindGroup(&bgDesc);
    MLG_CHECK(m_PresentPipeline.BindGroup0, "Failed to create bind group 0 for present pipeline");

    m_PresentPipeline.Pipeline = device.CreateRenderPipeline(&descriptor);
    MLG_CHECK(m_PresentPipeline.Pipeline, "Failed to create render pipeline for present pipeline");

    return Result<>::Ok;
}

Result<>
Renderer::CreateTransformPipeline()
{
    if(m_TransformPipeline.Pipeline)
    {
        return Result<>::Ok;
    }

    auto csResult = CreateShader(kTransformShader);
    MLG_CHECK(csResult);

    m_TransformPipeline.Shader = *csResult;

    auto bgLayouts = WebgpuHelper::GetTransformPipelineLayouts();
    MLG_CHECK(bgLayouts);

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "TransformPipelineLayout",
            .bindGroupLayoutCount = std::size(*bgLayouts),
            .bindGroupLayouts = bgLayouts->data(),
        };

    m_TransformPipeline.Layout =
        WebgpuHelper::GetDevice().CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(m_TransformPipeline.Layout, "Failed to create transform pipeline layout");

    const wgpu::ComputePipelineDescriptor pipelineDesc//
    {
        .layout = m_TransformPipeline.Layout,
        .compute//
        {
            .module = m_TransformPipeline.Shader,
            .entryPoint = "main",
        },
    };;

    m_TransformPipeline.Pipeline = WebgpuHelper::GetDevice().CreateComputePipeline(&pipelineDesc);
    MLG_CHECK(m_TransformPipeline.Pipeline, "Failed to create compute pipeline for transform");

    return Result<>::Ok;
}

Result<>
Renderer::TransformNodes(const wgpu::CommandEncoder& cmdEncoder,
    const TrsTransformf& camera,
    const Projection& projection,
    const Scene& scene) const
{
    const wgpu::Device device = WebgpuHelper::GetDevice();

    // Use inverse of camera transform as view matrix
    const Mat44f viewXform = camera.Inverse();
    const Mat44f& projMat = projection.GetMatrix();
    const Mat44f viewProj = projMat.Mul(viewXform);

    const ShaderInterop::CameraParams cameraParams //
        {
            .View = viewXform,
            .Projection = projMat,
            .ViewProj = viewProj,
        };

    auto cameraParamsBuf = scene.GetCameraParamsBuffer();

    device.GetQueue().WriteBuffer(
        cameraParamsBuf.GetGpuBuffer(),
        0,
        &cameraParams,
        sizeof(ShaderInterop::CameraParams));

    const wgpu::ComputePassEncoder pass = cmdEncoder.BeginComputePass();
    pass.SetPipeline(m_TransformPipeline.Pipeline);
    pass.SetBindGroup(0, scene.GetTransformPipelineBindGroup0());
    const uint32_t workgroupCountX = narrow_cast<uint32_t>(scene.GetModelInstances().size());
    pass.DispatchWorkgroups(workgroupCountX);
    pass.End();

    return Result<>::Ok;
}