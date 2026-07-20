#define MLG_LOGGER_NAME "CPAS"

#include "GpuColorPass.h"

#include "GpuHelper.h"
#include "PerfMetrics.h"

namespace
{

// Creates a bind group layout for the inputs of the color pass.
Result<wgpu::BindGroupLayout>
CreateInputsBindGroupLayout(const wgpu::Device& gpuDevice)
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

    wgpu::BindGroupLayout layout = gpuDevice.CreateBindGroupLayout(&desc);
    MLG_CHECK(layout, "Failed to create Inputs bind group layout");

    return layout;
}

Result<wgpu::PipelineLayout>
CreatePipelineLayout(const wgpu::Device& gpuDevice,
    const wgpu::BindGroupLayout& inputsBindGroupLayout,
    const wgpu::BindGroupLayout& textureBindGroupLayout)
{
    MLG_CHECK(inputsBindGroupLayout, "Inputs bind group layout is not valid");
    MLG_CHECK(textureBindGroupLayout, "Texture bind group layout is not valid");

    const wgpu::BindGroupLayout bindGroupLayouts[] //
        {
            inputsBindGroupLayout,
            textureBindGroupLayout,
        };

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "GpuColorPass",
            .bindGroupLayoutCount = std::size(bindGroupLayouts),
            .bindGroupLayouts = &bindGroupLayouts[0],
        };

    const wgpu::PipelineLayout pipelineLayout = gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(pipelineLayout, "Failed to create color pipeline layout");

    return pipelineLayout;
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
    auto shader = gpuHelper.LoadShader(ShaderPath, fileFetcher);
    MLG_CHECK(shader, "Failed to load shader: {}", ShaderPath);

    auto inputsBindGroupLayout = CreateInputsBindGroupLayout(gpuHelper.GetDevice());
    MLG_CHECK(inputsBindGroupLayout, "Failed to create Inputs bind group layout");

    auto textureBindGroupLayout = gpuHelper.GetTextureBindGroupLayout();
    MLG_CHECK(textureBindGroupLayout, "Failed to get texture bind group layout");

    auto pipelineLayout =
        CreatePipelineLayout(gpuHelper.GetDevice(), *inputsBindGroupLayout, textureBindGroupLayout);
    MLG_CHECK(pipelineLayout, "Failed to create pipeline layout");

    return GpuColorPass(gpuHelper, *shader, *inputsBindGroupLayout, *pipelineLayout);
}

Result<>
GpuColorPass::SetInputs(const Inputs& inputs)
{
    MLG_CHECK(inputs.Validate(), "Inputs are not valid");

    if(!m_Inputs || BindGroup0NeedsRefresh(*m_Inputs, inputs))
    {
        // Rebuild the bind group
        m_InputsBindGroup = {};
    }

    m_Inputs = inputs;

    return Result<>::Ok;
}

Result<>
GpuColorPass::SetOutputs(const Outputs& outputs)
{
    MLG_CHECK(outputs.Validate(), "Outputs are not valid");

    m_Outputs = outputs;

    return Result<>::Ok;
}

Result<wgpu::RenderPassEncoder>
GpuColorPass::BeginPass(const wgpu::CommandEncoder& cmdEncoder)
{
    MLG_CHECK(EnsurePipeline());
    MLG_CHECK(EnsureInputsBindGroup());

    MLG_CHECKV(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");
    MLG_CHECKV(m_Outputs, "Outputs are not valid - forget to call SetOutputs()?");

    const wgpu::RenderPassColorAttachment attachment //
        {
            .view = m_Outputs->RenderTarget->CreateView(),
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = wgpu::LoadOp::Clear,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
        };

    const wgpu::RenderPassDepthStencilAttachment depthStencilAttachment //
        {
            .view = m_Outputs->DepthBuffer->CreateView(),
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

    {
        MLG_SCOPED_TIMER("Renderer.Render.BeginRenderPass.SetBuffers");

        constexpr size_t kU16BitWidth = 16;
        constexpr size_t kU32BitWidth = 32;

        static_assert(VERTEX_INDEX_BITS == kU32BitWidth || VERTEX_INDEX_BITS == kU16BitWidth,
            "Unsupported index buffer format: only 16-bit and 32-bit indices are supported");

        constexpr wgpu::IndexFormat idxFmt = (VERTEX_INDEX_BITS == kU32BitWidth)
            ? wgpu::IndexFormat::Uint32
            : wgpu::IndexFormat::Uint16;

        renderPass.SetVertexBuffer(0,
            m_Inputs->Vertices.GetGpuBuffer(),
            0,
            m_Inputs->Vertices.BufferSize());

        renderPass.SetIndexBuffer(m_Inputs->Indices.GetGpuBuffer(),
            idxFmt,
            0,
            m_Inputs->Indices.BufferSize());
    }

    const Viewport& viewport = m_Inputs->Viewport;

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

    return renderPass;
}

// private:

Result<>
GpuColorPass::EnsurePipeline()
{
    if(m_Pipeline)
    {
        return Result<>::Ok;
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

    m_Pipeline = m_GpuHelper->GetDevice().CreateRenderPipeline(&descriptor);
    MLG_CHECK(m_Pipeline, "Failed to create render pipeline");

    return Result<>::Ok;
}

Result<>
GpuColorPass::EnsureInputsBindGroup()
{
    if(m_InputsBindGroup)
    {
        return Result<>::Ok;
    }

    MLG_CHECKV(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");

    const wgpu::BindGroupEntry entries[] //
        {
            {
                .binding = 0,
                .buffer = m_Inputs->WorldTransforms.GetGpuBuffer(),
                .offset = 0,
                .size = m_Inputs->WorldTransforms.BufferSize(),
            },
            {
                .binding = 1,
                .buffer = m_Inputs->ClipSpaceTransforms.GetGpuBuffer(),
                .offset = 0,
                .size = m_Inputs->ClipSpaceTransforms.BufferSize(),
            },
            {
                .binding = 2,
                .buffer = m_Inputs->MeshProperties.GetGpuBuffer(),
                .offset = 0,
                .size = m_Inputs->MeshProperties.BufferSize(),
            },
            {
                .binding = 3,
                .buffer = m_Inputs->MaterialConstants.GetGpuBuffer(),
                .offset = 0,
                .size = m_Inputs->MaterialConstants.BufferSize(),
            },
            {
                .binding = 4,
                .buffer = m_Inputs->CameraParams.GetGpuBuffer(),
                .offset = 0,
                .size = m_Inputs->CameraParams.BufferSize(),
            },
        };

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = "GpuColorPass::InputsBindGroup",
            .layout = m_InputsBindGroupLayout,
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    m_InputsBindGroup = m_GpuHelper->GetDevice().CreateBindGroup(&desc);
    MLG_CHECKV(m_InputsBindGroup, "Failed to create bind group");

    return Result<>::Ok;
}