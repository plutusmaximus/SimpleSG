#define MLG_LOGGER_NAME "CPAS"

#include "GpuColorPass.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "PerfMetrics.h"

#include <thread>
#include <webgpu/webgpu_cpp.h>

namespace
{

Result<>
LoadShaderCode(const char* filePath, std::vector<uint8_t>& outBuffer, FileFetcher& fileFetcher)
{
    FileFetcher::Request request(filePath);
    MLG_CHECK(fileFetcher.Fetch(request));

    while(request.IsPending())
    {
        MLG_CHECK(fileFetcher.ProcessCompletions());
        std::this_thread::yield();
    }

    MLG_CHECK(request.Succeeded(), "Failed to load shader file: {}", filePath);

    request.MoveDataTo(outBuffer);

    return Result<>::Ok;
}

Result<wgpu::ShaderModule>
CreateShader(const wgpu::Device& gpuDevice, const std::vector<uint8_t>& shaderCode)
{
    const void* data = shaderCode.data();
    const wgpu::StringView shaderCodeView{ static_cast<const char*>(data), shaderCode.size() };
    const wgpu::ShaderSourceWGSL wgsl{ { .nextInChain = nullptr, .code = shaderCodeView } };
    const wgpu::ShaderModuleDescriptor shaderModuleDescriptor //
        { .nextInChain = &wgsl, .label = "ColorShader" };

    wgpu::ShaderModule shaderModule = gpuDevice.CreateShaderModule(&shaderModuleDescriptor);
    MLG_CHECK(shaderModule, "Failed to create shader module");

    return shaderModule;
}

Result<std::array<wgpu::BindGroupLayout, 2>>
CreateLayouts(const wgpu::Device& gpuDevice)
{
    std::array<wgpu::BindGroupLayout, 2> layouts;

    {
        const wgpu::BindGroupLayoutEntry entries[]//
        {
            // World transform.
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::WorldTransform),
                },
            },
            // Clip transform.
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::ClipSpaceTransform),
                },
            },
            // Mesh properties.
            {
                .binding = 2,
                .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::MeshProperties),
                },
            },
            // Material constants buffer.
            {
                .binding = 3,
                .visibility = wgpu::ShaderStage::Fragment,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::ReadOnlyStorage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::MaterialConstants),
                },
            },
            // Camera parameters
            {
                .binding = 4,
                .visibility = wgpu::ShaderStage::Vertex,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::Uniform,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::CameraParams),
                },
            },
        };

        const wgpu::BindGroupLayoutDescriptor desc //
            {
                .label = "ColorShaderSceneGroupLayout",
                .entryCount = std::size(entries),
                .entries = &entries[0],
            };

        layouts[0] = gpuDevice.CreateBindGroupLayout(&desc);
    }

    {
        const wgpu::BindGroupLayoutEntry entries[]//
        {
            // Texture
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
            // Sampler
            {
                .binding = 1,
                .visibility = wgpu::ShaderStage::Fragment,
                .sampler =
                {
                    .type = wgpu::SamplerBindingType::Filtering,
                },
            },
        };

        const wgpu::BindGroupLayoutDescriptor desc = //
            {
                .label = "ColorShaderTextureGroupLayout",
                .entryCount = std::size(entries),
                .entries = &entries[0],
            };

        layouts[1] = gpuDevice.CreateBindGroupLayout(&desc);
    }

    return layouts;
}

wgpu::VertexBufferLayout
GetVertexBufferLayout()
{
    static const wgpu::VertexAttribute attributes[] = //
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

    static const wgpu::VertexBufferLayout layout = //
        {
            .stepMode = wgpu::VertexStepMode::Vertex,
            .arrayStride = sizeof(Vertex),
            .attributeCount = std::size(attributes),
            .attributes = &attributes[0],
        };

    return layout;
}

bool
BindGroup0NeedsRefresh(const GpuColorPass::Resources& currentResources,
    const GpuColorPass::Resources& newResources)
{
    return currentResources.WorldTransforms.GetGpuBuffer().Get()
        != newResources.WorldTransforms.GetGpuBuffer().Get()
        || currentResources.ClipSpaceTransforms.GetGpuBuffer().Get()
        != newResources.ClipSpaceTransforms.GetGpuBuffer().Get()
        || currentResources.MeshProperties.GetGpuBuffer().Get()
        != newResources.MeshProperties.GetGpuBuffer().Get()
        || currentResources.MaterialConstants.GetGpuBuffer().Get()
        != newResources.MaterialConstants.GetGpuBuffer().Get()
        || currentResources.CameraParams.GetGpuBuffer().Get()
        != newResources.CameraParams.GetGpuBuffer().Get();
}

} // namespace

Result<GpuColorPass>
GpuColorPass::Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
{
    GpuColorPass pass;

    std::vector<uint8_t> shaderCode;
    MLG_CHECK(LoadShaderCode(ShaderPath, shaderCode, fileFetcher));

    pass.m_ShaderCode = std::move(shaderCode);

    MLG_CHECK(pass.EnsurePipeline(gpuHelper.GetDevice()));

    return pass;
}

Result<GpuColorPass::TargetResources>
GpuColorPass::CreateTarget(const GpuHelper& gpuHelper, const uint32_t width, const uint32_t height)
{
    MLG_DEBUG("Creating new color target with size {}x{}", width, height);

    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

    const wgpu::TextureDescriptor targetTextureDesc //
        {
            .label = "ColorTarget",
            .usage = wgpu::TextureUsage::RenderAttachment
                | wgpu::TextureUsage::CopySrc         // For copying to compositor target
                | wgpu::TextureUsage::TextureBinding, // Also for copying to compositor target
            .dimension = wgpu::TextureDimension::e2D,
            .size = //
            {
                .width = width,
                .height = height,
                .depthOrArrayLayers = 1,
            },
            .format = kColorTargetFormat,
            .mipLevelCount = 1,
            .sampleCount = 1,
        };

    const wgpu::Texture target = gpuDevice.CreateTexture(&targetTextureDesc);
    MLG_CHECKV(target, "Failed to create color target texture");

    MLG_DEBUG("Creating new depth target with size {}x{}", width, height);

    const wgpu::TextureDescriptor depthTextureDesc //
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

    const wgpu::Texture depthTarget = gpuDevice.CreateTexture(&depthTextureDesc);
    MLG_CHECKV(depthTarget, "Failed to create depth target texture");

    return TargetResources //
        {
            .Target = target,
            .DepthTarget = depthTarget,
        };
}

Result<wgpu::BindGroup>
GpuColorPass::CreateTextureBindGroup(const GpuHelper& gpuHelper, const TextureResources& resources)
{
    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

    MLG_CHECKV(resources.Validate());
    MLG_CHECKV(m_PipelineResources.BindGroupLayouts[1]);

    const wgpu::BindGroupEntry entries[] = //
        {
            {
                .binding = 0,
                .textureView = resources.Texture.CreateView(),
            },
            {
                .binding = 1,
                .sampler = resources.Sampler,
            },
        };

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = "ColorShaderTextureGroupBindings",
            .layout = m_PipelineResources.BindGroupLayouts[1],
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    const wgpu::BindGroup bindGroup = gpuDevice.CreateBindGroup(&desc);
    MLG_CHECKV(bindGroup, "Failed to create texture bind group");

    return bindGroup;
}

Result<>
GpuColorPass::BindResources(
    const GpuHelper& gpuHelper, const Resources& resources, const TargetResources& targetResources)
{
    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

    MLG_CHECK(EnsurePipeline(gpuDevice));

    MLG_CHECKV(resources.Validate());
    MLG_CHECKV(targetResources.Validate());
    MLG_CHECKV(m_PipelineResources.BindGroupLayouts[0]);

    if(!m_BindGroup || !m_Resources || BindGroup0NeedsRefresh(*m_Resources, resources))
    {
        const wgpu::BindGroupEntry entries[] = //
            {
                {
                    .binding = 0,
                    .buffer = resources.WorldTransforms.GetGpuBuffer(),
                    .offset = 0,
                    .size = resources.WorldTransforms.BufferSize(),
                },
                {
                    .binding = 1,
                    .buffer = resources.ClipSpaceTransforms.GetGpuBuffer(),
                    .offset = 0,
                    .size = resources.ClipSpaceTransforms.BufferSize(),
                },
                {
                    .binding = 2,
                    .buffer = resources.MeshProperties.GetGpuBuffer(),
                    .offset = 0,
                    .size = resources.MeshProperties.BufferSize(),
                },
                {
                    .binding = 3,
                    .buffer = resources.MaterialConstants.GetGpuBuffer(),
                    .offset = 0,
                    .size = resources.MaterialConstants.BufferSize(),
                },
                {
                    .binding = 4,
                    .buffer = resources.CameraParams.GetGpuBuffer(),
                    .offset = 0,
                    .size = resources.CameraParams.BufferSize(),
                },
            };

        const wgpu::BindGroupDescriptor desc = //
            {
                .label = "ColorShaderSceneGroupBindings",
                .layout = m_PipelineResources.BindGroupLayouts[0],
                .entryCount = std::size(entries),
                .entries = &entries[0],
            };

        m_BindGroup = gpuDevice.CreateBindGroup(&desc);
        MLG_CHECKV(m_BindGroup, "Failed to create bind group");
    }

    m_Resources = resources;
    m_TargetResources = targetResources;

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuColorPass::BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder) const
{
    MLG_CHECK(m_TargetResources.Validate());
    MLG_CHECKV(m_Pipeline, "Pipeline is not valid");
    MLG_CHECKV(m_BindGroup, "Bind group is not valid");

    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = m_TargetResources.Target.CreateView(),
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
        };

    const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment //
        {
            .view = m_TargetResources.DepthTarget.CreateView(),
            .depthLoadOp = wgpu::LoadOp::Clear,
            .depthStoreOp = wgpu::StoreOp::Store,
            .depthClearValue = kClearDepth,
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

    {
        MLG_SCOPED_TIMER("Renderer.Render.BeginRenderPass.SetPipeline");

        renderPass.SetPipeline(m_Pipeline);
    }

    {
        MLG_SCOPED_TIMER("Renderer.Render.BeginRenderPass.SetPerFrameBindGroup");
        renderPass.SetBindGroup(0, m_BindGroup, 0, nullptr);
    }

    return renderPass;
}

// private:

Result<>
GpuColorPass::EnsurePipeline(const wgpu::Device& gpuDevice)
{
    if(m_Pipeline)
    {
        return Result<>::Ok;
    }

    if(!m_PipelineResources.Shader)
    {
        auto shader = CreateShader(gpuDevice, m_ShaderCode);
        MLG_CHECK(shader);

        m_PipelineResources.Shader = std::move(*shader);
    }

    if(!m_PipelineResources.BindGroupLayouts[0] || !m_PipelineResources.BindGroupLayouts[1])
    {
        auto layouts = CreateLayouts(gpuDevice);
        MLG_CHECK(layouts);

        m_PipelineResources.BindGroupLayouts = std::move(*layouts);
    }

    if(!m_PipelineResources.PipelineLayout)
    {
        const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
            {
                .label = "GpuColorPass::PipelineLayout",
                .bindGroupLayoutCount = std::size(m_PipelineResources.BindGroupLayouts),
                .bindGroupLayouts = m_PipelineResources.BindGroupLayouts.data(),
            };

        const wgpu::PipelineLayout pipelineLayout =
            gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
        MLG_CHECK(pipelineLayout, "Failed to create color pipeline layout");

        m_PipelineResources.PipelineLayout = std::move(pipelineLayout);
    }

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
            .format = kColorTargetFormat,
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    const wgpu::DepthStencilState depthStencilState //
        {
            .format = kDepthTargetFormat,
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
            .module = m_PipelineResources.Shader,
            .entryPoint = FragmentEntry,
            .targetCount = 1,
            .targets = &colorTargetState,
        };

    const wgpu::VertexBufferLayout vertexBufferLayout = GetVertexBufferLayout();

    const wgpu::RenderPipelineDescriptor descriptor//
    {
        .label = "GpuColorPass::Pipeline",
        .layout = m_PipelineResources.PipelineLayout,
        .vertex =
        {
            .module = m_PipelineResources.Shader,
            .entryPoint = VertexEntry,
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

    m_Pipeline = gpuDevice.CreateRenderPipeline(&descriptor);
    MLG_CHECK(m_Pipeline, "Failed to create render pipeline");

    return Result<>::Ok;
}