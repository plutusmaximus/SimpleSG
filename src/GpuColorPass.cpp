#include <string_view>
#define MLG_LOGGER_NAME "CPAS"

#include "GpuColorPass.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "PerfMetrics.h"

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

struct BindGroupLayouts
{
    wgpu::BindGroupLayout Inputs;
    wgpu::BindGroupLayout Texture;
};

Result<BindGroupLayouts>
CreateLayouts(const wgpu::Device& gpuDevice)
{
    BindGroupLayouts layouts;

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
                .label = "GpuColorPass::InputsBindGroupLayout",
                .entryCount = std::size(entries),
                .entries = &entries[0],
            };

        layouts.Inputs = gpuDevice.CreateBindGroupLayout(&desc);
        MLG_CHECK(layouts.Inputs, "Failed to create Inputs bind group layout");
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
                .label = "GpuColorPass::TextureBindGroupLayout",
                .entryCount = std::size(entries),
                .entries = &entries[0],
            };

        layouts.Texture = gpuDevice.CreateBindGroupLayout(&desc);
        MLG_CHECK(layouts.Texture, "Failed to create texture bind group layout");
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
BindGroup0NeedsRefresh(const GpuColorPass::Inputs& currentInputs,
    const GpuColorPass::Inputs& newInputs)
{
    return currentInputs.WorldTransforms.GetGpuBuffer().Get()
        != newInputs.WorldTransforms.GetGpuBuffer().Get()
        || currentInputs.ClipSpaceTransforms.GetGpuBuffer().Get()
        != newInputs.ClipSpaceTransforms.GetGpuBuffer().Get()
        || currentInputs.MeshProperties.GetGpuBuffer().Get()
        != newInputs.MeshProperties.GetGpuBuffer().Get()
        || currentInputs.MaterialConstants.GetGpuBuffer().Get()
        != newInputs.MaterialConstants.GetGpuBuffer().Get()
        || currentInputs.CameraParams.GetGpuBuffer().Get()
        != newInputs.CameraParams.GetGpuBuffer().Get();
}

} // namespace

Result<GpuColorPass>
GpuColorPass::Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
{
    GpuColorPass pass;

    auto shader = LoadShader(ShaderPath, gpuHelper.GetDevice(), fileFetcher);
    MLG_CHECK(shader);

    pass.m_Shader = std::move(*shader);

    MLG_CHECK(pass.EnsurePipeline(gpuHelper.GetDevice()));

    return pass;
}

Result<wgpu::BindGroup>
GpuColorPass::CreateTextureBindGroup(const GpuHelper& gpuHelper, const TextureResources& resources)
{
    MLG_CHECKV(resources.Validate());
    MLG_CHECKV(m_TextureBindGroupLayout, "Texture bind group layout is not valid");

    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

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
            .label = "GpuColorPass::TextureBindGroup",
            .layout = m_TextureBindGroupLayout,
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    const wgpu::BindGroup bindGroup = gpuDevice.CreateBindGroup(&desc);
    MLG_CHECKV(bindGroup, "Failed to create texture bind group");

    return bindGroup;
}

Result<>
GpuColorPass::BindInputs(const GpuHelper& gpuHelper, const Inputs& inputs)
{
    MLG_CHECKV(inputs.Validate());
    MLG_CHECKV(m_InputsBindGroupLayout, "Inputs bind group layout is not valid");

    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

    if(!m_InputsBindGroup || !m_Inputs || BindGroup0NeedsRefresh(*m_Inputs, inputs))
    {
        const wgpu::BindGroupEntry entries[] = //
            {
                {
                    .binding = 0,
                    .buffer = inputs.WorldTransforms.GetGpuBuffer(),
                    .offset = 0,
                    .size = inputs.WorldTransforms.BufferSize(),
                },
                {
                    .binding = 1,
                    .buffer = inputs.ClipSpaceTransforms.GetGpuBuffer(),
                    .offset = 0,
                    .size = inputs.ClipSpaceTransforms.BufferSize(),
                },
                {
                    .binding = 2,
                    .buffer = inputs.MeshProperties.GetGpuBuffer(),
                    .offset = 0,
                    .size = inputs.MeshProperties.BufferSize(),
                },
                {
                    .binding = 3,
                    .buffer = inputs.MaterialConstants.GetGpuBuffer(),
                    .offset = 0,
                    .size = inputs.MaterialConstants.BufferSize(),
                },
                {
                    .binding = 4,
                    .buffer = inputs.CameraParams.GetGpuBuffer(),
                    .offset = 0,
                    .size = inputs.CameraParams.BufferSize(),
                },
            };

        const wgpu::BindGroupDescriptor desc = //
            {
                .label = "GpuColorPass::InputsBindGroup",
                .layout = m_InputsBindGroupLayout,
                .entryCount = std::size(entries),
                .entries = &entries[0],
            };

        m_InputsBindGroup = gpuDevice.CreateBindGroup(&desc);
        MLG_CHECKV(m_InputsBindGroup, "Failed to create bind group");
    }

    m_Inputs = inputs;

    return Result<>::Ok;
}

Result<>
GpuColorPass::BindOutputs(const GpuHelper& /*gpuHelper*/, const Outputs& outputs)
{
    MLG_CHECKV(outputs.Validate());
    
    m_Outputs = outputs;

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuColorPass::BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder) const
{
    MLG_CHECK(m_Outputs.Validate());
    MLG_CHECKV(m_Pipeline, "Pipeline is not valid");
    MLG_CHECKV(m_InputsBindGroup, "Inputs bind group is not valid");

    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = m_Outputs.RenderTarget.CreateView(),
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
        };

    const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment //
        {
            .view = m_Outputs.DepthBuffer.CreateView(),
            .depthLoadOp = wgpu::LoadOp::Clear,
            .depthStoreOp = wgpu::StoreOp::Store,
            .depthClearValue = kClearDepth,
            .stencilLoadOp = wgpu::LoadOp::Undefined,
            .stencilStoreOp = wgpu::StoreOp::Undefined,
            .stencilClearValue = 0,
        };

    const wgpu::RenderPassDescriptor renderPassDesc //
        {
            .label = "GpuColorPass",
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
        renderPass.SetBindGroup(0, m_InputsBindGroup, 0, nullptr);
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

    MLG_CHECKV(m_Shader, "Shader is not valid");

    if(!m_InputsBindGroupLayout || !m_TextureBindGroupLayout)
    {
        auto layouts = CreateLayouts(gpuDevice);
        MLG_CHECK(layouts);

        m_InputsBindGroupLayout = std::move(layouts->Inputs);
        m_TextureBindGroupLayout = std::move(layouts->Texture);
    }

    if(!m_PipelineLayout)
    {
        const wgpu::BindGroupLayout bindGroupLayouts[] = { m_InputsBindGroupLayout, m_TextureBindGroupLayout };

        const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
            {
                .label = "GpuColorPass",
                .bindGroupLayoutCount = std::size(bindGroupLayouts),
                .bindGroupLayouts = &bindGroupLayouts[0],
            };

        const wgpu::PipelineLayout pipelineLayout =
            gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
        MLG_CHECK(pipelineLayout, "Failed to create color pipeline layout");

        m_PipelineLayout = std::move(pipelineLayout);
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
            .format = GpuHelper::kTextureFormat,
            .blend = &blendState,
            .writeMask = wgpu::ColorWriteMask::All,
        };

    const wgpu::DepthStencilState depthStencilState //
        {
            .format = GpuHelper::kDepthBufferFormat,
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
            .module = m_Shader,
            .entryPoint = FragmentEntry,
            .targetCount = 1,
            .targets = &colorTargetState,
        };

    const wgpu::VertexBufferLayout vertexBufferLayout = GetVertexBufferLayout();

    const wgpu::RenderPipelineDescriptor descriptor//
    {
        .label = "GpuColorPass",
        .layout = m_PipelineLayout,
        .vertex =
        {
            .module = m_Shader,
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