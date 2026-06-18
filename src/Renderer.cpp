#define MLG_LOGGER_NAME "DAWN"

#include "Renderer.h"

#include "Compositor.h"
#include "FileFetcher.h"
#include "PropKit.h"
#include "narrow_cast.h"
#include "PerfMetrics.h"
#include "Camera.h"
#include "Scene.h"
#include "scope_exit.h"
#include "ShaderInterop.h"
#include "WebgpuHelper.h"

#include <thread>
#include <ranges>

namespace
{
constexpr const char* kCompositorShader = "shaders/CompositorShader.wgsl";

constexpr const char* kColorShader = "shaders/ColorShader.wgsl";

constexpr const char* kTransformShader = "shaders/TransformShader.wgsl";

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

    const Extent screenBounds = WebgpuHelper::GetScreenBounds();

    MLG_CHECK(RefreshColorTargetResources(static_cast<uint32_t>(screenBounds.Width),
        static_cast<uint32_t>(screenBounds.Height)));
    MLG_CHECK(CreateColorPipeline());
    MLG_CHECK(CreateCompositorPipeline());
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

    m_ColorTargetResources = {};
    m_ColorPipelineResources = {};
    m_ColorPipeline = {};
    m_TransformPipelineResources = {};
    m_TransformPipeline = {};
    m_CompositorPipelineResources = {};
    m_CompositorPipeline = {};

    m_Initialized = false;

    return Result<>::Ok;
}

Result<>
Renderer::Render(const Camera& camera,
    const Posef& cameraXForm,
    const Scene& scene,
    const PropKit& propKit)
{
    MLG_CHECKV(m_Initialized, "Renderer is not initialized");

    const Viewport& viewport = camera.GetViewport();

    MLG_SCOPED_TIMER("Renderer.Render");

    MLG_CHECK(RefreshColorTargetResources(viewport.GetWidth(), viewport.GetHeight()));

    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "RenderCommandEncoder" };

    const wgpu::CommandEncoder cmdEncoder = WebgpuHelper::GetDevice().CreateCommandEncoder(&encoderDesc);
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    {
        MLG_SCOPED_TIMER("Renderer.Render.TransformNodes");

        auto transformNodesResult = TransformNodes(cmdEncoder, cameraXForm, camera, scene);
        MLG_CHECK(transformNodesResult);
    }

    wgpu::RenderPassEncoder renderPass;
    {
        MLG_SCOPED_TIMER("Renderer.Render.BeginRenderPass");
        auto renderPassResult = BeginRenderPass(cmdEncoder);
        MLG_CHECK(renderPassResult);

        renderPass = *renderPassResult;
    }

    renderPass.SetViewport(static_cast<float>(viewport.GetX()),
        static_cast<float>(viewport.GetY()),
        static_cast<float>(viewport.GetWidth()),
        static_cast<float>(viewport.GetHeight()),
        viewport.GetMinDepth(),
        viewport.GetMaxDepth());

    renderPass.SetScissorRect(viewport.GetX(),
        viewport.GetY(),
        viewport.GetWidth(),
        viewport.GetHeight());

    {
        MLG_SCOPED_TIMER("Renderer.Render.SetPipeline");

        renderPass.SetPipeline(m_ColorPipeline);
    }

    {
        MLG_SCOPED_TIMER("Renderer.Render.Draw.SetPerFrameBindGroup");
        renderPass.SetBindGroup(0, scene.GetColorShaderBindGroup(), 0, nullptr);
    }

    {
        MLG_SCOPED_TIMER("Renderer.Render.Draw.SetBuffers");

        constexpr size_t kU16BitWidth = 16;
        constexpr size_t kU32BitWidth = 32;

        static_assert(VERTEX_INDEX_BITS == kU32BitWidth || VERTEX_INDEX_BITS == kU16BitWidth,
            "Unsupported index buffer format: only 16-bit and 32-bit indices are supported");

        constexpr wgpu::IndexFormat idxFmt =
            (VERTEX_INDEX_BITS == kU32BitWidth)
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

    {
        MLG_SCOPED_TIMER("Renderer.Render.Draw")

        uint64_t indirectOffset = 0;

        const double modelCount = static_cast<double>(scene.GetModelInstances().size());

        static PerfCounter pcTotalModels("Renderer.Render.Models.Total",
            PerfCounter::SamplePolicy::ResetOnSample);
        
        pcTotalModels.Increment(modelCount);

        const auto& drawIndirectBuffer = scene.GetDrawIndirectBuffer();
        const Frustum frustum(camera, cameraXForm);

        MaterialIdentifier lastMaterialId{};

        const auto view = std::views::zip(scene.GetModelInstances(), scene.GetWorldTransforms());

        for(const auto&& [modelInstance, worldXForm] : view)
        {
            const ModelIdentifier modelId = modelInstance.GetModelId();
            auto meshes = propKit.GetMeshes(modelId);
            MLG_CHECKV(meshes);

            const Vec4f& pos = worldXForm.Transform[3];

            auto sphere = propKit.GetBoundingSphere(modelId);
            MLG_CHECK(sphere);

            if(!modelInstance.IsVisible() ||
                !frustum.Contains(*sphere + Vec3f(pos.x, pos.y, pos.z)))
            {
                indirectOffset += meshes->size() * sizeof(ShaderInterop::DrawIndirectParams);
                continue;
            }

            static PerfCounter pcCulledModels("Renderer.Render.Models.Visible",
                PerfCounter::SamplePolicy::ResetOnSample);
            pcCulledModels.Increment(1);

            for(const auto &mesh : *meshes)
            {
                const MaterialIdentifier materialId = mesh.GetMaterialId();

                if(materialId != lastMaterialId)
                {
                    MLG_SCOPED_TIMER("Renderer.Render.Draw.SetMaterialBindGroup");

                    const wgpu::BindGroup* materialBindGroup =
                        propKit.GetMaterialBindGroup(materialId);
                    MLG_CHECKV(materialBindGroup, "Failed to get material bind group");

                    renderPass.SetBindGroup(1, *materialBindGroup, 0, nullptr);
                    lastMaterialId = materialId;
                }

                {
                    MLG_SCOPED_TIMER("Renderer.Render.Draw.DrawIndexed");

                    renderPass.DrawIndexedIndirect(drawIndirectBuffer.GetGpuBuffer(), indirectOffset);
                    indirectOffset += sizeof(ShaderInterop::DrawIndirectParams);
                }
            }
        }
    }

    renderPass.End();

    wgpu::CommandBuffer cmdBuf;
    {
        MLG_SCOPED_TIMER("Renderer.FinishCommandBuffer");
        cmdBuf = cmdEncoder.Finish(nullptr);

        MLG_CHECK(cmdBuf, "Failed to finish command buffer");
    }

    {
        MLG_SCOPED_TIMER("Renderer.SubmitCommandBuffer");
        const wgpu::Queue queue = WebgpuHelper::GetDevice().GetQueue();
        MLG_CHECK(queue, "Failed to get wgpu::Queue");

        queue.Submit(1, &cmdBuf);
    }

    return Result<>::Ok;
}

Result<>
Renderer::GetTarget(wgpu::Texture& outTexture, wgpu::TextureView& outTextureView) const
{
    MLG_CHECKV(m_Initialized, "Renderer is not initialized");

    outTexture = m_ColorTargetResources.Target;
    outTextureView = m_ColorTargetResources.TargetView;
    return Result<>::Ok;
}

Result<> Renderer::Composite(Compositor& compositor) const
{
    MLG_CHECKV(m_Initialized, "Renderer is not initialized");

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

    const wgpu::CommandEncoder cmdEncoder = compositor.GetCommandEncoder();

    const wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);

    renderPass.SetPipeline(m_CompositorPipeline);
    renderPass.SetBindGroup(0, m_ColorTargetResources.BindGroup, 0, nullptr);
    renderPass.Draw(3, 1, 0, 0);
    renderPass.End();

    return Result<>::Ok;
}

//private:

Result<wgpu::RenderPassEncoder>
Renderer::BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder)
{
    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = m_ColorTargetResources.TargetView,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
        };

    static constexpr float CLEAR_DEPTH = 1.0f;

    const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment //
        {
            .view = m_ColorTargetResources.DepthTargetView,
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

    return renderPass;
}

Result<>
Renderer::RefreshColorTargetResources(const uint32_t width, const uint32_t height)
{
    static constexpr wgpu::TextureFormat kDepthTargetFormat = wgpu::TextureFormat::Depth24Plus;

    if(!m_ColorTargetResources.Target ||
        m_ColorTargetResources.Target.GetWidth() != width ||
        m_ColorTargetResources.Target.GetHeight() != height)
    {
        MLG_DEBUG("Creating new color target with size {}x{}", width, height);

        // Release the bind group so it will be recreated below.
        m_ColorTargetResources.BindGroup = {};

        const wgpu::TextureDescriptor textureDesc //
            {
                .label = "ColorTarget",
                .usage = wgpu::TextureUsage::RenderAttachment | wgpu::TextureUsage::CopySrc |
                         wgpu::TextureUsage::TextureBinding,
                .dimension = wgpu::TextureDimension::e2D,
                .size = //
                {
                    .width = width,
                    .height = height,
                    .depthOrArrayLayers = 1,
                },
                .format = WebgpuHelper::GetSwapChainFormat(),
                .mipLevelCount = 1,
                .sampleCount = 1,
            };

        m_ColorTargetResources.Target = WebgpuHelper::GetDevice().CreateTexture(&textureDesc);
        m_ColorTargetResources.TargetView = m_ColorTargetResources.Target.CreateView();
    }

    if(!m_ColorTargetResources.DepthTarget ||
        m_ColorTargetResources.DepthTarget.GetWidth() != width ||
        m_ColorTargetResources.DepthTarget.GetHeight() != height)
    {
        MLG_DEBUG("Creating new depth target with size {}x{}", width, height);

        const wgpu::TextureDescriptor textureDesc //
            {
                .label = "DepthTarget",
                .usage = wgpu::TextureUsage::RenderAttachment,
                .dimension = wgpu::TextureDimension::e2D,
                .size = //
                {
                    .width = width,
                    .height = height,
                    .depthOrArrayLayers = 1,
                },
                .format = kDepthTargetFormat,
                .mipLevelCount = 1,
                .sampleCount = 1,
            };

        m_ColorTargetResources.DepthTarget = WebgpuHelper::GetDevice().CreateTexture(&textureDesc);
        m_ColorTargetResources.DepthTargetView = m_ColorTargetResources.DepthTarget.CreateView();
    }

    if(!m_ColorTargetResources.Sampler)
    {
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

        m_ColorTargetResources.Sampler = WebgpuHelper::GetDevice().CreateSampler(&samplerDesc);
    }

    // Create bind group for the color target texture and sampler

    if(!m_ColorTargetResources.BindGroup)
    {
        auto bgLayout = WebgpuHelper::GetTextureSamplerBindGroupLayout();
        MLG_CHECK(bgLayout);

        const wgpu::BindGroupEntry bgEntries[] = //
            {
                {
                    .binding = 0,
                    .textureView = m_ColorTargetResources.TargetView,
                },
                {
                    .binding = 1,
                    .sampler = m_ColorTargetResources.Sampler,
                },
            };

        const wgpu::BindGroupDescriptor bgDesc //
            {
                .label = "ColorTargetTextureBindGroup",
                .layout = *bgLayout,
                .entryCount = std::size(bgEntries),
                .entries = &bgEntries[0],
            };

        m_ColorTargetResources.BindGroup = WebgpuHelper::GetDevice().CreateBindGroup(&bgDesc);
        MLG_CHECK(m_ColorTargetResources.BindGroup, "Failed to create bind group 0 for present pipeline");
    }

    return Result<>::Ok;
}

Result<>
Renderer::CreateColorPipeline()
{
    if(m_ColorPipeline)
    {
        return Result<>::Ok;
    }

    MLG_CHECKV(m_ColorTargetResources.Target, "Color target is invalid");
    MLG_CHECKV(m_ColorTargetResources.DepthTarget, "Depth target is invalid");

    auto shader = CreateShader(kColorShader);
    MLG_CHECK(shader);

    m_ColorPipelineResources.Shader = *shader;

    // Color target pipeline layout

    auto bgLayouts = WebgpuHelper::GetColorPipelineLayouts();
    MLG_CHECK(bgLayouts);

    const wgpu::PipelineLayoutDescriptor colorTargetPipelineLayoutDesc //
        {
            .label = "ColorPipelineLayout",
            .bindGroupLayoutCount = std::size(*bgLayouts),
            .bindGroupLayouts = bgLayouts->data(),
        };

    m_ColorPipelineResources.Layout =
        WebgpuHelper::GetDevice().CreatePipelineLayout(&colorTargetPipelineLayoutDesc);
    MLG_CHECK(m_ColorPipelineResources.Layout, "Failed to create color pipeline layout");

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
            .format = m_ColorTargetResources.Target.GetFormat(),
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    const wgpu::DepthStencilState depthStencilState //
        {
            .format = m_ColorTargetResources.DepthTarget.GetFormat(),
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
            .module = m_ColorPipelineResources.Shader,
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
        .layout = m_ColorPipelineResources.Layout,
        .vertex =
        {
            .module = m_ColorPipelineResources.Shader,
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

    m_ColorPipeline = WebgpuHelper::GetDevice().CreateRenderPipeline(&descriptor);
    MLG_CHECK(m_ColorPipeline, "Failed to create render pipeline");

    return Result<>::Ok;
}

Result<>
Renderer::CreateCompositorPipeline()
{
    if(m_CompositorPipeline)
    {
        return Result<>::Ok;
    }

    auto shader = CreateShader(kCompositorShader);
    MLG_CHECK(shader);

    m_CompositorPipelineResources.Shader = *shader;

    auto bgLayout = WebgpuHelper::GetTextureSamplerBindGroupLayout();
    MLG_CHECK(bgLayout);

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "CompositorPipelineLayout",
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bgLayout.Value(),
        };

    m_CompositorPipelineResources.Layout =
        WebgpuHelper::GetDevice().CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(m_CompositorPipelineResources.Layout, "Failed to create compositor pipeline layout");

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
            .module = m_CompositorPipelineResources.Shader,
            .entryPoint = "fs_main",
            .targetCount = 1,
            .targets = &colorTargetState,
        };

    const wgpu::RenderPipelineDescriptor descriptor//
    {
        .label = "CompositorPipeline",
        .layout = m_CompositorPipelineResources.Layout,
        .vertex =
        {
            .module = m_CompositorPipelineResources.Shader,
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

    m_CompositorPipeline = WebgpuHelper::GetDevice().CreateRenderPipeline(&descriptor);
    MLG_CHECK(m_CompositorPipeline, "Failed to create compositor pipeline");

    return Result<>::Ok;
}

Result<>
Renderer::CreateTransformPipeline()
{
    if(m_TransformPipeline)
    {
        return Result<>::Ok;
    }

    auto csResult = CreateShader(kTransformShader);
    MLG_CHECK(csResult);

    m_TransformPipelineResources.Shader = *csResult;

    auto bgLayouts = WebgpuHelper::GetTransformPipelineLayouts();
    MLG_CHECK(bgLayouts);

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "TransformPipelineLayout",
            .bindGroupLayoutCount = std::size(*bgLayouts),
            .bindGroupLayouts = bgLayouts->data(),
        };

    m_TransformPipelineResources.Layout =
        WebgpuHelper::GetDevice().CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(m_TransformPipelineResources.Layout, "Failed to create transform pipeline layout");

    const wgpu::ComputePipelineDescriptor pipelineDesc//
    {
        .layout = m_TransformPipelineResources.Layout,
        .compute//
        {
            .module = m_TransformPipelineResources.Shader,
            .entryPoint = "main",
        },
    };;

    m_TransformPipeline = WebgpuHelper::GetDevice().CreateComputePipeline(&pipelineDesc);
    MLG_CHECK(m_TransformPipeline, "Failed to create compute pipeline for transform");

    return Result<>::Ok;
}

Result<>
Renderer::TransformNodes(const wgpu::CommandEncoder& cmdEncoder,
    const Posef& cameraXForm,
    const Camera& camera,
    const Scene& scene) const
{
    const wgpu::Device device = WebgpuHelper::GetDevice();

    // Use inverse of camera transform as view matrix
    const Mat44f viewXform = cameraXForm.Inverse().ToMatrix();
    const Mat44f& projMat = camera.GetMatrix();
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
    pass.SetPipeline(m_TransformPipeline);
    pass.SetBindGroup(0, scene.GetTransformShaderBindGroup());
    const uint32_t workgroupCountX = narrow_cast<uint32_t>(scene.GetModelInstances().size());
    pass.DispatchWorkgroups(workgroupCountX);
    pass.End();

    return Result<>::Ok;
}