#define MLG_LOGGER_NAME "CMPP"

#include "GpuCompositorPass.h"
#include "GpuHelper.h"

namespace
{

Result<wgpu::Sampler>
CreateSampler(const GpuHelper& gpuHelper)
{
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

    const wgpu::Sampler sampler = gpuHelper.GetDevice().CreateSampler(&samplerDesc);
    MLG_CHECK(sampler, "Failed to create sampler");

    return sampler;
}

Result<wgpu::BindGroupLayout>
CreateBindGroupLayout(const GpuHelper& gpuHelper)
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

    const wgpu::BindGroupLayoutDescriptor desc //
        {
            .label = "GpuCompositorPass",
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    const wgpu::BindGroupLayout bindGroupLayout =
        gpuHelper.GetDevice().CreateBindGroupLayout(&desc);
    MLG_CHECK(bindGroupLayout, "Failed to create bind group layout");

    return bindGroupLayout;
}

Result<wgpu::PipelineLayout>
CreatePipelineLayout(const GpuHelper& gpuHelper, const wgpu::BindGroupLayout& bindGroupLayout)
{
    MLG_CHECKV(bindGroupLayout, "Bind group layout is not valid");

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "GpuCompositorPass",
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bindGroupLayout,
        };

    const wgpu::PipelineLayout pipelineLayout =
        gpuHelper.GetDevice().CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(pipelineLayout, "Failed to create pipeline layout");
    return pipelineLayout;
}
} // namespace

Result<GpuCompositorPass>
GpuCompositorPass::Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
{
    auto shader = gpuHelper.LoadShader(ShaderPath, fileFetcher);
    MLG_CHECK(shader, "Failed to load shader: {}", ShaderPath);

    auto sampler = CreateSampler(gpuHelper);
    MLG_CHECK(sampler);

    auto bindGroupLayout = CreateBindGroupLayout(gpuHelper);
    MLG_CHECK(bindGroupLayout);

    auto pipelineLayout = CreatePipelineLayout(gpuHelper, *bindGroupLayout);
    MLG_CHECK(pipelineLayout);

    GpuCompositorPass pass(gpuHelper, *shader, *sampler, *bindGroupLayout, *pipelineLayout);

    return pass;
}

Result<>
GpuCompositorPass::SetInputs(const Inputs& inputs)
{
    MLG_CHECK(inputs.Validate(), "Inputs are not valid");

    if(inputs != m_Inputs)
    {
        if(!m_Inputs || inputs.Texture != m_Inputs->Texture)
        {
            // Rebuild the bind group
            m_InputsBindGroup = {};
        }

        m_Inputs = inputs;
    }

    return Result<>::Ok;
}

Result<>
GpuCompositorPass::SetOutputs(const Outputs& outputs)
{
    MLG_CHECK(outputs.Validate(), "Outputs are not valid");

    if(outputs != m_Outputs)
    {
        if(!m_Outputs || outputs.RenderTarget->GetFormat() != m_Outputs->RenderTarget->GetFormat())
        {
            // Rebuild the pipeline
            m_Pipeline = {};
        }

        m_Outputs = outputs;
    }

    return Result<>::Ok;
}

Result<GpuCompositorPass::Invocation>
GpuCompositorPass::Prepare()
{
    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "GpuCompositorPass" };
    wgpu::CommandEncoder cmdEncoder = m_GpuHelper->GetDevice().CreateCommandEncoder(&encoderDesc);
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    auto invocation = Prepare(cmdEncoder);

    if(invocation)
    {
        // We own the encoder - hand it over to the invocation so it can submit the command buffer
        // when Execute() is called.
        invocation->m_CmdEncoder = std::move(cmdEncoder);
    }

    return invocation;
}

Result<GpuCompositorPass::Invocation>
GpuCompositorPass::Prepare(const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_CHECK(EnsurePipeline());
    MLG_CHECK(EnsureInputsBindGroup());

    MLG_CHECK(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");
    MLG_CHECK(m_Outputs, "Outputs are not valid - forget to call SetOutputs()?");

    MLG_CHECKV(m_Outputs->RenderTarget.Get() != m_Inputs->Texture,
        "Output texture must be different from input texture");

    const Rect targetRect({ .X = 0,
        .Y = 0,
        .Width = m_Outputs->RenderTarget->GetWidth(),
        .Height = m_Outputs->RenderTarget->GetHeight() });

    Rect dstRect = m_Inputs->DstRect;

    if(!targetRect.Contains(m_Inputs->DstRect))
    {
        MLG_CHECKV(targetRect.Intersects(m_Inputs->DstRect), "DstRect is outside of target rect");

        dstRect = targetRect.Intersect(m_Inputs->DstRect);
        MLG_WARN("dstRect clipped");
    }

    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = m_Outputs->RenderTarget->CreateView(),
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
    renderPass.SetBindGroup(0, m_InputsBindGroup, 0, nullptr);

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

    return GpuCompositorPass::Invocation(m_GpuHelper->GetDevice(), std::move(renderPass));
}

// private:

Result<>
GpuCompositorPass::EnsurePipeline()
{
    if(m_Pipeline)
    {
        return Result<>::Ok;
    }

    MLG_CHECK(m_Outputs, "Outputs are not valid - forget to call SetOutputs()?");

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
            .format = m_Outputs->RenderTarget->GetFormat(),
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    const wgpu::FragmentState fragmentState //
        {
            .module = m_Shader,
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
            .module = m_Shader,
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

    m_Pipeline = m_GpuHelper->GetDevice().CreateRenderPipeline(&desc);
    MLG_CHECK(m_Pipeline, "Failed to create pipeline");

    return Result<>::Ok;
}

Result<>
GpuCompositorPass::EnsureInputsBindGroup()
{
    if(m_InputsBindGroup)
    {
        return Result<>::Ok;
    }

    MLG_CHECK(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");

    const wgpu::BindGroupEntry entries[] //
        {
            {
                .binding = 0,
                .textureView = m_Inputs->Texture.CreateView(),
            },
            {
                .binding = 1,
                .sampler = m_Sampler,
            },
        };

    const wgpu::BindGroupDescriptor desc //
        {
            .label = "GpuCompositorPass::Inputs",
            .layout = m_BindGroupLayout,
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    m_InputsBindGroup = m_GpuHelper->GetDevice().CreateBindGroup(&desc);
    MLG_CHECK(m_InputsBindGroup, "Failed to create bind group");

    return Result<>::Ok;
}

// GpuCompositorPass::Pass

GpuCompositorPass::Invocation::~Invocation()
{
    MLG_ASSERT(!m_RenderPass, "Pass must be executed before destruction");
}

Result<>
GpuCompositorPass::Invocation::Execute()
{
    MLG_CHECKV(m_RenderPass, "Pass has already been executed");

    // Consume the render pass so it can't be used again.
    const wgpu::RenderPassEncoder renderPass = std::move(m_RenderPass);

    m_RenderPass = {};

    renderPass.Draw(3, 1, 0, 0);
    renderPass.End();

    // If m_CmdEncoder is null then it's owned by the caller and they are responsible for submitting
    // it to the GPU. Otherwise, we own it and we will submit it to the GPU here.
    if(m_CmdEncoder)
    {
        const wgpu::CommandBuffer cmdBuf = m_CmdEncoder.Finish(nullptr);
        MLG_CHECK(cmdBuf, "Failed to finish command buffer");

        const wgpu::Queue queue = m_GpuDevice.GetQueue();
        MLG_CHECK(queue, "Failed to get wgpu::Queue");
        queue.Submit(1, &cmdBuf);
    }

    return Result<>::Ok;
}