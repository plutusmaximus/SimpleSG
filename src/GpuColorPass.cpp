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

} // namespace

Result<GpuColorPass>
GpuColorPass::Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
{
    GpuColorPass pass;

    std::vector<uint8_t> shaderCode;
    MLG_CHECK(LoadShaderCode(ShaderPath, shaderCode, fileFetcher));

    std::vector<uint8_t> compositorShaderCode;
    MLG_CHECK(LoadShaderCode(CompositorShaderPath, compositorShaderCode, fileFetcher));

    pass.m_ShaderCode = std::move(shaderCode);
    pass.m_CompositorShaderCode = std::move(compositorShaderCode);

    const Dimension2 screenDimensions = gpuHelper.GetScreenDimensions();

    const uint32_t width = static_cast<uint32_t>(screenDimensions.Width);
    const uint32_t height = static_cast<uint32_t>(screenDimensions.Height);

    MLG_CHECK(pass.Ensure(gpuHelper, width, height));

    return pass;
}

Result<>
GpuColorPass::Ensure(const GpuHelper& gpuHelper, const uint32_t width, const uint32_t height)
{
    const wgpu::TextureFormat targetFormat = gpuHelper.GetSwapChainFormat();

    MLG_CHECK(EnsureTarget(gpuHelper.GetDevice(), width, height, targetFormat));
    MLG_CHECK(EnsurePipeline(gpuHelper.GetDevice()));
    MLG_CHECK(EnsureCompositorPipeline(gpuHelper.GetDevice(), targetFormat));
    return Result<>::Ok;
}

Result<wgpu::Texture>
GpuColorPass::GetTarget() const
{
    MLG_CHECKV(m_TargetResources.Target, "Target texture is not valid");
    
    return m_TargetResources.Target;
}

Result<>
GpuColorPass::BindResources(const wgpu::Device& gpuDevice, const Resources& resources)
{
    MLG_CHECKV(m_PipelineResources.BindGroupLayouts[0], "Bind group layout is not valid");
    MLG_CHECKV(resources.WorldTransforms, "World transforms buffer is not valid");
    MLG_CHECKV(resources.ClipSpaceTransforms, "Clip space transforms buffer is not valid");
    MLG_CHECKV(resources.MeshProperties, "Mesh properties buffer is not valid");
    MLG_CHECKV(resources.MaterialConstants, "Material constants buffer is not valid");
    MLG_CHECKV(resources.CameraParams, "Camera params buffer is not valid");

    if(m_Resources == resources)
    {
        // Resources are already bound, nothing to do
        return Result<>::Ok;
    }

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

    m_Resources = resources;

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuColorPass::BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder)
{
    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = m_TargetResources.TargetView,
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
        };

    const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment //
        {
            .view = m_TargetResources.DepthTargetView,
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

Result<wgpu::RenderPassEncoder>
GpuColorPass::BeginCompositorRenderPass(const wgpu::CommandEncoder& cmdEncoder,
    wgpu::Texture target)
{
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

    //const wgpu::CommandEncoder cmdEncoder = compositor.GetCommandEncoder();

    const wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);
    MLG_CHECK(renderPass, "Failed to begin compositor render pass");

    renderPass.SetPipeline(m_CompositorPipeline);
    renderPass.SetBindGroup(0, m_CompositorBindGroup, 0, nullptr);

    return renderPass;
}

// private:

Result<>
GpuColorPass::EnsureTarget(const wgpu::Device& gpuDevice,
    const uint32_t width,
    const uint32_t height,
    wgpu::TextureFormat targetFormat)
{
    if(!m_TargetResources.Target
        || m_TargetResources.Target.GetWidth()
        != width
        || m_TargetResources.Target.GetHeight()
        != height)
    {
        MLG_DEBUG("Creating new color target with size {}x{}", width, height);

        const wgpu::TextureDescriptor textureDesc //
            {
                .label = "ColorTarget",
                .usage = wgpu::TextureUsage::RenderAttachment
                    | wgpu::TextureUsage::CopySrc
                    | wgpu::TextureUsage::TextureBinding,
                .dimension = wgpu::TextureDimension::e2D,
                .size = //
                {
                    .width = width,
                    .height = height,
                    .depthOrArrayLayers = 1,
                },
                .format = targetFormat,
                .mipLevelCount = 1,
                .sampleCount = 1,
            };

        m_TargetResources.Target = gpuDevice.CreateTexture(&textureDesc);
        m_TargetResources.TargetView = m_TargetResources.Target.CreateView();

        m_CompositorBindGroup = {};
    }

    if(!m_TargetResources.DepthTarget
        || m_TargetResources.DepthTarget.GetWidth()
        != width
        || m_TargetResources.DepthTarget.GetHeight()
        != height)
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

        m_TargetResources.DepthTarget = gpuDevice.CreateTexture(&textureDesc);
        m_TargetResources.DepthTargetView = m_TargetResources.DepthTarget.CreateView();
    }

    return Result<>::Ok;
}

Result<>
GpuColorPass::EnsurePipeline(const wgpu::Device& gpuDevice)
{
    if(m_Pipeline
        && m_PipelineResources.TargetFormat
        == m_TargetResources.Target.GetFormat()
        && m_PipelineResources.DepthFormat
        == m_TargetResources.DepthTarget.GetFormat())
    {
        return Result<>::Ok;
    }

    auto shader = CreateShader(gpuDevice, m_ShaderCode);
    MLG_CHECK(shader);

    auto layouts = CreateLayouts(gpuDevice);
    MLG_CHECK(layouts);

    const wgpu::BindGroupLayout bgLayouts[]{ (*layouts)[0], (*layouts)[1] };

    const wgpu::PipelineLayoutDescriptor colorTargetPipelineLayoutDesc //
        {
            .label = "ColorPipelineLayout",
            .bindGroupLayoutCount = std::size(bgLayouts),
            .bindGroupLayouts = &bgLayouts[0],
        };

    const wgpu::PipelineLayout pipelineLayout =
        gpuDevice.CreatePipelineLayout(&colorTargetPipelineLayoutDesc);
    MLG_CHECK(pipelineLayout, "Failed to create color pipeline layout");

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
            .format = m_TargetResources.Target.GetFormat(),
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    const wgpu::DepthStencilState depthStencilState //
        {
            .format = m_TargetResources.DepthTarget.GetFormat(),
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
            .module = *shader,
            .entryPoint = FragmentEntry,
            .targetCount = 1,
            .targets = &colorTargetState,
        };

    const wgpu::VertexBufferLayout vertexBufferLayout = GetVertexBufferLayout();

    const wgpu::RenderPipelineDescriptor descriptor//
    {
        .label = "ColorTargetPipeline",
        .layout = pipelineLayout,
        .vertex =
        {
            .module = *shader,
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

    m_PipelineResources.Shader = std::move(*shader);
    m_PipelineResources.BindGroupLayouts = std::move(*layouts);
    m_PipelineResources.Layout = std::move(pipelineLayout);
    m_PipelineResources.TargetFormat = m_TargetResources.Target.GetFormat();
    m_PipelineResources.DepthFormat = m_TargetResources.DepthTarget.GetFormat();

    return Result<>::Ok;
}

Result<>
GpuColorPass::EnsureCompositorPipeline(const wgpu::Device& gpuDevice,
    const wgpu::TextureFormat targetFormat)
{
    if(!m_CompositorPipelineResources.Sampler)
    {
        const wgpu::SamplerDescriptor samplerDesc //
            {
                .label = "Compositor",
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

        m_CompositorPipelineResources.Sampler = gpuDevice.CreateSampler(&samplerDesc);
        MLG_CHECK(m_CompositorPipelineResources.Sampler, "Failed to create compositor sampler");
    }

    if(!m_CompositorPipelineResources.BindGroupLayout)
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
                .label = "Compositor",
                .entryCount = std::size(entries),
                .entries = &entries[0],
            };

        m_CompositorPipelineResources.BindGroupLayout = gpuDevice.CreateBindGroupLayout(&desc);
        MLG_CHECK(m_CompositorPipelineResources.BindGroupLayout, "Failed to create compositor bind group layout");
    }

    if(!m_CompositorBindGroup)
    {
        const wgpu::BindGroupEntry entries[] = //
            {
                {
                    .binding = 0,
                    .textureView = m_TargetResources.TargetView,
                },
                {
                    .binding = 1,
                    .sampler = m_CompositorPipelineResources.Sampler,
                },
            };

        const wgpu::BindGroupDescriptor desc = //
            {
                .label = "Compositor",
                .layout = m_CompositorPipelineResources.BindGroupLayout,
                .entryCount = std::size(entries),
                .entries = &entries[0],
            };

        m_CompositorBindGroup = gpuDevice.CreateBindGroup(&desc);
    }

    if(!m_CompositorPipeline || m_CompositorPipelineResources.TargetFormat != targetFormat)
    {
        auto shader = CreateShader(gpuDevice, m_CompositorShaderCode);
        MLG_CHECK(shader);

        const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
            {
                .label = "Compositor",
                .bindGroupLayoutCount = 1,
                .bindGroupLayouts = &m_CompositorPipelineResources.BindGroupLayout,
            };

        const wgpu::PipelineLayout pipelineLayout =
            gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
        MLG_CHECK(pipelineLayout, "Failed to create compositor pipeline layout");

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
                .format = targetFormat,
                .blend = &blendState,
                .writeMask = wgpu::ColorWriteMask::All,
            };

        const wgpu::FragmentState fragmentState //
            {
                .module = *shader,
                .entryPoint = "fs_main",
                .targetCount = 1,
                .targets = &colorTargetState,
            };

        const wgpu::RenderPipelineDescriptor descriptor//
        {
            .label = "CompositorPipeline",
            .layout = pipelineLayout,
            .vertex =
            {
                .module = *shader,
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

        m_CompositorPipelineResources.Shader = *shader;
        m_CompositorPipelineResources.Layout = pipelineLayout;
        m_CompositorPipelineResources.TargetFormat = targetFormat;

        m_CompositorPipeline = gpuDevice.CreateRenderPipeline(&descriptor);
        MLG_CHECK(m_CompositorPipeline, "Failed to create compositor pipeline");
    }

    return Result<>::Ok;
}

// GpuCompositorPass

Result<GpuCompositorPass>
GpuCompositorPass::Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
{
    GpuCompositorPass pass;

    std::vector<uint8_t> shaderCode;
    MLG_CHECK(LoadShaderCode(ShaderPath, shaderCode, fileFetcher));

    pass.m_ShaderCode = std::move(shaderCode);

    MLG_CHECK(pass.EnsurePipeline(gpuHelper));

    return pass;
}

Result<>
GpuCompositorPass::BindResources(const wgpu::Device& gpuDevice, const Resources& resources)
{
    if(m_Resources == resources)
    {
        return Result<>::Ok;
    }

    MLG_CHECKV(resources.SourceTexture, "Source texture is not valid");
    MLG_CHECKV(m_PipelineResources.Sampler, "Sampler is not valid");
    MLG_CHECKV(m_PipelineResources.BindGroupLayout, "Bind group layout is not valid");

    const wgpu::BindGroupEntry entries[] = //
        {
            {
                .binding = 0,
                .textureView = resources.SourceTexture.CreateView(),
            },
            {
                .binding = 1,
                .sampler = m_PipelineResources.Sampler,
            },
        };

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = "Compositor",
            .layout = m_PipelineResources.BindGroupLayout,
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    m_BindGroup = gpuDevice.CreateBindGroup(&desc);

    MLG_CHECK(m_BindGroup, "Failed to create compositor bind group");

    m_Resources = resources;

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuCompositorPass::BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder, wgpu::Texture target)
{
    MLG_CHECKV(m_Pipeline, "Pipeline is not valid");
    MLG_CHECKV(m_BindGroup, "Bind group is not valid");

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

    //const wgpu::CommandEncoder cmdEncoder = compositor.GetCommandEncoder();

    const wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);
    MLG_CHECK(renderPass, "Failed to begin compositor render pass");

    renderPass.SetPipeline(m_Pipeline);
    renderPass.SetBindGroup(0, m_BindGroup, 0, nullptr);

    return renderPass;
}

// private:

Result<>
GpuCompositorPass::EnsurePipeline(const GpuHelper& gpuHelper)
{
    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();
    const wgpu::TextureFormat targetFormat = gpuHelper.GetSwapChainFormat();

    if(!m_PipelineResources.Sampler)
    {
        const wgpu::SamplerDescriptor samplerDesc //
            {
                .label = "Compositor",
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

        m_PipelineResources.Sampler = gpuDevice.CreateSampler(&samplerDesc);
        MLG_CHECK(m_PipelineResources.Sampler, "Failed to create compositor sampler");
    }

    if(!m_PipelineResources.BindGroupLayout)
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
                .label = "Compositor",
                .entryCount = std::size(entries),
                .entries = &entries[0],
            };

        m_PipelineResources.BindGroupLayout = gpuDevice.CreateBindGroupLayout(&desc);
        MLG_CHECK(m_PipelineResources.BindGroupLayout, "Failed to create compositor bind group layout");
    }

    if(!m_Pipeline || m_PipelineResources.TargetFormat != targetFormat)
    {
        auto shader = CreateShader(gpuDevice, m_ShaderCode);
        MLG_CHECK(shader);

        const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
            {
                .label = "Compositor",
                .bindGroupLayoutCount = 1,
                .bindGroupLayouts = &m_PipelineResources.BindGroupLayout,
            };

        const wgpu::PipelineLayout pipelineLayout =
            gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
        MLG_CHECK(pipelineLayout, "Failed to create compositor pipeline layout");

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
                .format = targetFormat,
                .blend = &blendState,
                .writeMask = wgpu::ColorWriteMask::All,
            };

        const wgpu::FragmentState fragmentState //
            {
                .module = *shader,
                .entryPoint = "fs_main",
                .targetCount = 1,
                .targets = &colorTargetState,
            };

        const wgpu::RenderPipelineDescriptor descriptor//
        {
            .label = "CompositorPipeline",
            .layout = pipelineLayout,
            .vertex =
            {
                .module = *shader,
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

        m_PipelineResources.Shader = *shader;
        m_PipelineResources.Layout = pipelineLayout;
        m_PipelineResources.TargetFormat = targetFormat;

        m_Pipeline = gpuDevice.CreateRenderPipeline(&descriptor);
        MLG_CHECK(m_Pipeline, "Failed to create compositor pipeline");
    }

    return Result<>::Ok;
}