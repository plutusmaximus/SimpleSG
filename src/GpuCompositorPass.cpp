#define MLG_LOGGER_NAME "CMPP"

#include "GpuCompositorPass.h"

#include "FileFetcher.h"
#include "GpuHelper.h"

#include <filesystem>
#include <thread>
#include <webgpu/webgpu_cpp.h>

namespace
{

Result<wgpu::ShaderModule>
LoadShader(const char* filePath, const wgpu::Device& gpuDevice, FileFetcher& fileFetcher)
{
    FileFetcher::Request request(filePath);
    MLG_CHECK(fileFetcher.Fetch(request));

    while(request.IsPending())
    {
        MLG_CHECK(fileFetcher.ProcessCompletions());
        std::this_thread::yield();
    }

    MLG_CHECK(request.Succeeded(), "Failed to load shader file: {}", filePath);

    const std::string filename = std::filesystem::path(filePath).filename().string();
    const std::span<const uint8_t> data = request.GetData();

    const void* dataPtr = data.data();
    const wgpu::StringView shaderCode{ static_cast<const char*>(dataPtr), data.size() };
    const wgpu::StringView label = std::string_view(filename);
    const wgpu::ShaderSourceWGSL wgsl{ { .code = shaderCode } };
    const wgpu::ShaderModuleDescriptor desc 
        { .nextInChain = &wgsl, .label = label };

    wgpu::ShaderModule shaderModule = gpuDevice.CreateShaderModule(&desc);
    MLG_CHECK(shaderModule, "Failed to create shader module");

    return shaderModule;
}

} // namespace

Result<GpuCompositorPass>
GpuCompositorPass::Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
{
    GpuCompositorPass pass;

    auto shader = LoadShader(ShaderPath, gpuHelper.GetDevice(), fileFetcher);
    MLG_CHECK(shader);

    pass.m_Shader = std::move(*shader);

    MLG_CHECK(pass.EnsureSampler(gpuHelper.GetDevice()));
    MLG_CHECK(pass.EnsureBindGroupLayout(gpuHelper.GetDevice()));

    return pass;
}

Result<>
GpuCompositorPass::SetInputs(const GpuHelper& gpuHelper, const Inputs& inputs)
{
    MLG_CHECKV(inputs.Validate());
    MLG_CHECKV(m_Sampler, "Sampler is not valid");
    MLG_CHECKV(m_BindGroupLayout, "Bind group layout is not valid");

    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

    if(!m_BindGroup || inputs != m_Inputs)
    {
        const wgpu::BindGroupEntry entries[] = //
            {
                {
                    .binding = 0,
                    .textureView = inputs.Texture.CreateView(),
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
GpuCompositorPass::SetOutputs(const GpuHelper& gpuHelper, const Outputs& outputs)
{
    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

    MLG_CHECKV(outputs.Validate());

    if(outputs != m_Outputs)
    {
        // Rebuild the pipeline
        m_Pipeline = {};
    }

    m_Outputs = outputs;

    MLG_CHECK(EnsurePipeline(gpuDevice));

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuCompositorPass::BeginPass(const wgpu::CommandEncoder& cmdEncoder) const
{
    MLG_CHECK(m_Inputs.Validate(), "Inputs are not valid - forget to call SetInputs()?");
    MLG_CHECK(m_Outputs.Validate(), "Outputs are not valid - forget to call SetOutputs()?");
    MLG_CHECKV(m_Pipeline, "Pipeline is not valid");
    MLG_CHECKV(m_BindGroup, "Bind group is not valid ");

    const Rect targetRect({ .X = 0,
        .Y = 0,
        .Width = m_Outputs.Texture.GetWidth(),
        .Height = m_Outputs.Texture.GetHeight() });

    Rect dstRect = m_Inputs.DstRect;

    if(!targetRect.Contains(m_Inputs.DstRect))
    {
        MLG_CHECKV(targetRect.Intersects(m_Inputs.DstRect), "DstRect is outside of target rect");
        
        dstRect = targetRect.Intersect(m_Inputs.DstRect);
        MLG_WARN("dstRect clipped");
    }

    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = m_Outputs.Texture.CreateView(),
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
GpuCompositorPass::Composite(const GpuHelper& gpuHelper) const
{
    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

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
GpuCompositorPass::EnsureSampler(const wgpu::Device& gpuDevice)
{
    if(!m_Sampler)
    {
        const wgpu::SamplerDescriptor samplerDesc //
            {
                .label = "GpuCompositorPass",
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

        m_Sampler = gpuDevice.CreateSampler(&samplerDesc);
        MLG_CHECK(m_Sampler, "Failed to create sampler");
    }

    return Result<>::Ok;
}


Result<>
GpuCompositorPass::EnsureBindGroupLayout(const wgpu::Device& gpuDevice)
{
    if(!m_BindGroupLayout)
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
GpuCompositorPass::EnsurePipeline(const wgpu::Device& gpuDevice)
{
    if(m_Pipeline)
    {
        return Result<>::Ok;
    }

    MLG_CHECKV(m_Shader, "Shader is not valid");

    MLG_CHECK(EnsureSampler(gpuDevice));
    MLG_CHECK(EnsureBindGroupLayout(gpuDevice));

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

    const wgpu::ColorTargetState colorTargetState //
        {
            .format = m_Outputs.Texture.GetFormat(),
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

    m_Pipeline = gpuDevice.CreateRenderPipeline(&desc);
    MLG_CHECK(m_Pipeline, "Failed to create pipeline");

    return Result<>::Ok;
}
