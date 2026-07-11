#define MLG_LOGGER_NAME "CMPP"

#include "GpuCompositorPass.h"

#include "FileFetcher.h"
#include "GpuHelper.h"

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
        { .nextInChain = &wgsl, .label = "CompositorShader" };

    wgpu::ShaderModule shaderModule = gpuDevice.CreateShaderModule(&shaderModuleDescriptor);
    MLG_CHECK(shaderModule, "Failed to create shader module");

    return shaderModule;
}

} // namespace

Result<GpuCompositorPass>
GpuCompositorPass::Create(const GpuHelper& /*gpuHelper*/, FileFetcher& fileFetcher)
{
    GpuCompositorPass pass;

    std::vector<uint8_t> shaderCode;
    MLG_CHECK(LoadShaderCode(ShaderPath, shaderCode, fileFetcher));

    pass.m_ShaderCode = std::move(shaderCode);

    return pass;
}

Result<>
GpuCompositorPass::BindResources(const GpuHelper& gpuHelper, const Resources& resources)
{
    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

    MLG_CHECK(EnsurePipeline(gpuDevice, resources.TargetTexture.GetFormat()));

    MLG_CHECKV(resources.Validate());
    MLG_CHECKV(m_PipelineResources.BindGroupLayout, "Pipeline bind group layout is not valid");
    MLG_CHECKV(m_PipelineResources.Sampler, "Sampler is not valid");

    if(!m_BindGroup || m_Resources.SourceTexture.Get() != resources.SourceTexture.Get())
    {
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
        MLG_CHECK(m_BindGroup, "Failed to create bind group");
    }

    m_Resources = resources;

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuCompositorPass::BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder) const
{
    MLG_CHECK(m_Resources.Validate(), "Resources are not valid - forget to call BindResources()?");
    MLG_CHECKV(m_Pipeline, "Pipeline is not valid - forget to call BindResources()?");
    MLG_CHECKV(m_BindGroup, "Bind group is not valid - forget to call BindResources()?");

    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = m_Resources.TargetTexture.CreateView(),
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
        };

    const wgpu::RenderPassDescriptor renderPassDesc //
        {
            .label = "Compositor",
            .colorAttachmentCount = 1,
            .colorAttachments = &attachment,
        };

    const wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);
    MLG_CHECK(renderPass, "Failed to begin render pass");

    renderPass.SetPipeline(m_Pipeline);
    renderPass.SetBindGroup(0, m_BindGroup, 0, nullptr);

    return renderPass;
}

Result<>
GpuCompositorPass::Composite(const GpuHelper& gpuHelper, const wgpu::Texture& target) const
{
    const Rect dstRect(
        { .X = 0, .Y = 0, .Width = target.GetWidth(), .Height = target.GetHeight() });
    return Composite(gpuHelper, target, dstRect);
}

Result<>
GpuCompositorPass::Composite(
    const GpuHelper& gpuHelper, const wgpu::Texture& target, const Rect& maybeDstRect) const
{
    MLG_CHECK(m_Resources.Validate(), "Resources are not valid - forget to call BindResources()?");
    MLG_CHECKV(m_Pipeline, "Pipeline is not valid - forget to call BindResources()?");
    MLG_CHECKV(m_BindGroup, "Bind group is not valid - forget to call BindResources()?");

    const Rect targetRect(
        { .X = 0, .Y = 0, .Width = target.GetWidth(), .Height = target.GetHeight() });

    Rect dstRect = maybeDstRect;

    if(!targetRect.Contains(maybeDstRect))
    {
        dstRect = targetRect.Intersect(maybeDstRect);
        MLG_WARN("dstRect clipped");
    }

    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "Compositor" };
    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder(&encoderDesc);
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    auto renderPass = BeginRenderPass(cmdEncoder);
    MLG_CHECK(renderPass, "Failed to begin render pass");

    {
        const float x = static_cast<float>(dstRect.GetX());
        const float y = static_cast<float>(dstRect.GetY());
        const float width = static_cast<float>(dstRect.GetWidth());
        const float height = static_cast<float>(dstRect.GetHeight());

        renderPass->SetViewport(x, y, width, height, 0, 1);
    }
    {
        const uint32_t x = static_cast<uint32_t>(dstRect.GetX());
        const uint32_t y = static_cast<uint32_t>(dstRect.GetY());
        const uint32_t width = static_cast<uint32_t>(dstRect.GetWidth());
        const uint32_t height = static_cast<uint32_t>(dstRect.GetHeight());

        renderPass->SetScissorRect(x, y, width, height);
    }

    renderPass->Draw(3, 1, 0, 0);
    renderPass->End();

    const wgpu::CommandBuffer cmdBuf = cmdEncoder.Finish(nullptr);
    MLG_CHECK(cmdBuf, "Failed to finish command buffer");

    const wgpu::Queue queue = gpuDevice.GetQueue();
    MLG_CHECK(queue, "Failed to get wgpu::Queue");
    queue.Submit(1, &cmdBuf);

    return Result<>::Ok;
}

// private:

Result<>
GpuCompositorPass::EnsurePipeline(const wgpu::Device& gpuDevice,
    const wgpu::TextureFormat targetFormat)
{
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
        MLG_CHECK(m_PipelineResources.Sampler, "Failed to create sampler");
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
        MLG_CHECK(m_PipelineResources.BindGroupLayout, "Failed to create bind group layout");
    }

    if(!m_PipelineResources.Shader)
    {
        auto shader = CreateShader(gpuDevice, m_ShaderCode);
        MLG_CHECK(shader);

        m_PipelineResources.Shader = std::move(*shader);
    }

    if(!m_PipelineResources.Layout)
    {
        const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
            {
                .label = "Compositor",
                .bindGroupLayoutCount = 1,
                .bindGroupLayouts = &m_PipelineResources.BindGroupLayout,
            };

        m_PipelineResources.Layout = gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
        MLG_CHECK(m_PipelineResources.Layout, "Failed to create pipeline layout");
    }

    if(!m_Pipeline || m_PipelineResources.TargetFormat != targetFormat)
    {
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
                .module = m_PipelineResources.Shader,
                .entryPoint = FragmentEntry,
                .targetCount = 1,
                .targets = &colorTargetState,
            };

        const wgpu::RenderPipelineDescriptor descriptor//
        {
            .label = "Compositor",
            .layout = m_PipelineResources.Layout,
            .vertex =
            {
                .module = m_PipelineResources.Shader,
                .entryPoint = VertexEntry,
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

        m_PipelineResources.TargetFormat = targetFormat;

        m_Pipeline = gpuDevice.CreateRenderPipeline(&descriptor);
        MLG_CHECK(m_Pipeline, "Failed to create pipeline");
    }

    return Result<>::Ok;
}
