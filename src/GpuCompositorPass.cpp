#define MLG_LOGGER_NAME "CMPP"

#include "GpuCompositorPass.h"

#include "GpuHelper.h"

Result<GpuCompositorPass>
GpuCompositorPass::Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
{
    auto shader = gpuHelper.LoadShader(ShaderPath, fileFetcher);
    MLG_CHECK(shader);

    GpuCompositorPass pass(gpuHelper, std::move(*shader));

    MLG_CHECK(pass.EnsureSampler());
    MLG_CHECK(pass.EnsureBindGroupLayout());

    return pass;
}

Result<>
GpuCompositorPass::SetInputs(const Inputs& inputs)
{
    MLG_CHECKV(m_Sampler, "Sampler is not valid");
    MLG_CHECKV(m_BindGroupLayout, "Bind group layout is not valid");

    const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

    if(!m_BindGroup || inputs != m_Inputs)
    {
        const wgpu::BindGroupEntry entries[] = //
            {
                {
                    .binding = 0,
                    .textureView = inputs.Texture->CreateView(),
                },
                {
                    .binding = 1,
                    .sampler = m_Sampler,
                },
            };

        const wgpu::BindGroupDescriptor desc = //
            {
                .label = "GpuCompositorPass::Inputs",
                .layout = m_BindGroupLayout,
                .entryCount = std::size(entries),
                .entries = &entries[0],
            };

        m_BindGroup = gpuDevice.CreateBindGroup(&desc);
        MLG_CHECK(m_BindGroup, "Failed to create bind group");
    }

    m_Inputs = inputs;

    return Result<>::Ok;
}

Result<>
GpuCompositorPass::SetOutputs(const Outputs& outputs)
{
    if(outputs != m_Outputs)
    {
        // Rebuild the pipeline
        m_Pipeline = {};
    }

    m_Outputs = outputs;

    MLG_CHECK(EnsurePipeline());

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuCompositorPass::BeginPass(const wgpu::CommandEncoder& cmdEncoder) const
{
    MLG_CHECKV(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");
    MLG_CHECKV(m_Outputs, "Outputs are not valid - forget to call SetOutputs()?");
    MLG_CHECKV(m_Pipeline, "Pipeline is not valid");
    MLG_CHECKV(m_BindGroup, "Bind group is not valid ");

    const Rect targetRect({ .X = 0,
        .Y = 0,
        .Width = m_Outputs->Texture->GetWidth(),
        .Height = m_Outputs->Texture->GetHeight() });

    Rect dstRect = m_Inputs->DstRect;

    if(!targetRect.Contains(m_Inputs->DstRect))
    {
        MLG_CHECKV(targetRect.Intersects(m_Inputs->DstRect), "DstRect is outside of target rect");
        
        dstRect = targetRect.Intersect(m_Inputs->DstRect);
        MLG_WARN("dstRect clipped");
    }

    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = m_Outputs->Texture->CreateView(),
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
        };

    const wgpu::RenderPassDescriptor renderPassDesc //
        {
            .label = "GpuCompositorPass",
            .colorAttachmentCount = 1,
            .colorAttachments = &attachment,
        };

    const wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);
    MLG_CHECK(renderPass, "Failed to begin render pass");

    renderPass.SetPipeline(m_Pipeline);
    renderPass.SetBindGroup(0, m_BindGroup, 0, nullptr);

    {
        const float x = static_cast<float>(dstRect.GetX());
        const float y = static_cast<float>(dstRect.GetY());
        const float width = static_cast<float>(dstRect.GetWidth());
        const float height = static_cast<float>(dstRect.GetHeight());

        renderPass.SetViewport(x, y, width, height, 0, 1);
    }
    {
        const uint32_t x = static_cast<uint32_t>(dstRect.GetX());
        const uint32_t y = static_cast<uint32_t>(dstRect.GetY());
        const uint32_t width = static_cast<uint32_t>(dstRect.GetWidth());
        const uint32_t height = static_cast<uint32_t>(dstRect.GetHeight());

        renderPass.SetScissorRect(x, y, width, height);
    }

    return renderPass;
}

Result<>
GpuCompositorPass::Composite() const
{
    const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "GpuCompositorPass" };
    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder(&encoderDesc);
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    auto pass = BeginPass(cmdEncoder);
    MLG_CHECK(pass, "Failed to begin render pass");

    pass->Draw(3, 1, 0, 0);
    pass->End();

    const wgpu::CommandBuffer cmdBuf = cmdEncoder.Finish(nullptr);
    MLG_CHECK(cmdBuf, "Failed to finish command buffer");

    const wgpu::Queue queue = gpuDevice.GetQueue();
    MLG_CHECK(queue, "Failed to get wgpu::Queue");
    queue.Submit(1, &cmdBuf);

    return Result<>::Ok;
}

// private:

Result<>
GpuCompositorPass::EnsureSampler()
{
    if(!m_Sampler)
    {
        const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

        const wgpu::SamplerDescriptor samplerDesc //
            {
                .label = "GpuCompositorPass",
                .addressModeU = wgpu::AddressMode::ClampToEdge,
                .addressModeV = wgpu::AddressMode::ClampToEdge,
                .addressModeW = wgpu::AddressMode::Undefined,
                .magFilter = wgpu::FilterMode::Nearest,
                .minFilter = wgpu::FilterMode::Nearest,
                .mipmapFilter = wgpu::MipmapFilterMode::Undefined,
                .lodMinClamp = 0.0f,
                .lodMaxClamp = 32.0f,
                .compare = wgpu::CompareFunction::Undefined,
                .maxAnisotropy = 1,
            };

        m_Sampler = gpuDevice.CreateSampler(&samplerDesc);
        MLG_CHECK(m_Sampler, "Failed to create sampler");
    }

    return Result<>::Ok;
}


Result<>
GpuCompositorPass::EnsureBindGroupLayout()
{
    if(!m_BindGroupLayout)
    {
        const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

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
                .label = "GpuCompositorPass",
                .entryCount = std::size(entries),
                .entries = &entries[0],
            };

        m_BindGroupLayout = gpuDevice.CreateBindGroupLayout(&desc);
        MLG_CHECK(m_BindGroupLayout, "Failed to create bind group layout");
    }

    return Result<>::Ok;
}

Result<>
GpuCompositorPass::EnsurePipeline()
{
    if(m_Pipeline)
    {
        return Result<>::Ok;
    }

    MLG_CHECK(EnsureSampler());
    MLG_CHECK(EnsureBindGroupLayout());

    const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

    if(!m_PipelineLayout)
    {
        const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
            {
                .label = "GpuCompositorPass",
                .bindGroupLayoutCount = 1,
                .bindGroupLayouts = &m_BindGroupLayout,
            };

        m_PipelineLayout = gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
        MLG_CHECK(m_PipelineLayout, "Failed to create pipeline layout");
    }

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

    MLG_CHECKV(m_Outputs, "Outputs are not valid - forget to call SetOutputs()?");
    
    const wgpu::ColorTargetState colorTargetState //
        {
            .format = m_Outputs->Texture->GetFormat(),
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    const wgpu::FragmentState fragmentState //
        {
            .module = *m_Shader,
            .entryPoint = FragmentEntry,
            .targetCount = 1,
            .targets = &colorTargetState,
        };

    const wgpu::RenderPipelineDescriptor desc//
    {
        .label = "GpuCompositorPass",
        .layout = m_PipelineLayout,
        .vertex =
        {
            .module = *m_Shader,
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

    m_Pipeline = gpuDevice.CreateRenderPipeline(&desc);
    MLG_CHECK(m_Pipeline, "Failed to create pipeline");

    return Result<>::Ok;
}
