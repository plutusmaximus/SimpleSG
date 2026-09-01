#define MLG_LOGGER_NAME "CPAS"

#include "GpuColorPass.h"

#include "GpuHelper.h"
#include "PerfMetrics.h"
#include "PropKit.h"

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
        // Mesh instance parameters.
        {
            .binding = 2,
            .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
            .buffer =
            {
                .type = wgpu::BufferBindingType::ReadOnlyStorage,
                .hasDynamicOffset = false,
                .minBindingSize = sizeof(ShaderInterop::MeshInstanceParams),
            },
        },
        // Camera parameters
        {
            .binding = 3,
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
    const wgpu::BindGroupLayout& materialBindGroupLayout)
{
    MLG_CHECK(inputsBindGroupLayout, "Inputs bind group layout is not valid");
    MLG_CHECK(materialBindGroupLayout, "Material bind group layout is not valid");

    const wgpu::BindGroupLayout bindGroupLayouts[] //
        {
            inputsBindGroupLayout,
            materialBindGroupLayout,
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
        || currentInputs.MeshInstanceParams.GetGpuBuffer().Get()
        != newInputs.MeshInstanceParams.GetGpuBuffer().Get()
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

    const wgpu::BindGroupLayout materialBindGroupLayout = gpuHelper.GetMaterialBindGroupLayout();
    MLG_CHECK(materialBindGroupLayout, "Failed to get material bind group layout");

    auto pipelineLayout = CreatePipelineLayout(gpuHelper.GetDevice(),
        *inputsBindGroupLayout,
        materialBindGroupLayout);
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

Result<GpuColorPass::Invocation>
GpuColorPass::Prepare()
{
    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "GpuColorPass" };
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

Result<GpuColorPass::Invocation>
GpuColorPass::Prepare(const wgpu::CommandEncoder& cmdEncoder)
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

    const wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);
    MLG_CHECK(renderPass, "Failed to begin render pass");

    {
        MLG_SCOPED_TIMER("GpuColorPass.Prepare.SetPipeline");

        renderPass.SetPipeline(m_Pipeline);
    }

    {
        MLG_SCOPED_TIMER("GpuColorPass.Prepare.SetPerFrameBindGroup");
        renderPass.SetBindGroup(0, m_InputsBindGroup, 0, nullptr);
    }

    {
        MLG_SCOPED_TIMER("GpuColorPass.Prepare.SetBuffers");

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

    return Invocation(m_GpuHelper->GetDevice(), std::move(renderPass));
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

    const wgpu::VertexBufferLayout vertexBufferLayouts[] //
        {
            GetVertexBufferLayout(),
        };

    const wgpu::RenderPipelineDescriptor descriptor//
    {
        .label = "GpuColorPass",
        .layout = m_PipelineLayout,
        .vertex =
        {
            .module = m_Shader,
            .entryPoint = VertexEntry,
            .bufferCount = 1,
            .buffers = &vertexBufferLayouts[0],
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
                .buffer = m_Inputs->MeshInstanceParams.GetGpuBuffer(),
                .offset = 0,
                .size = m_Inputs->MeshInstanceParams.BufferSize(),
            },
            {
                .binding = 3,
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

// GpuColorPass::Invocation

GpuColorPass::Invocation::~Invocation()
{
    MLG_ASSERT(!m_RenderPass, "Pass must be executed before destruction");
}

Result<>
GpuColorPass::Invocation::Execute(const std::span<MeshInstance> visibleMeshes,
    const PropKit& propKit)
{
    MLG_SCOPED_TIMER("GpuColorPass.Execute")

    MLG_CHECKV(m_RenderPass, "Pass has already been executed");

    // Consume the render pass so it can't be used again.
    const wgpu::RenderPassEncoder renderPass = std::move(m_RenderPass);

    m_RenderPass = {};

    // Track how many times we have to change materials.
    static PerfCounter pcMaterialChanges({ .Name = "GpuColorPass.Execute.MaterialChanges" });

    MaterialIdentifier lastMaterialId;

    for(const MeshInstance& meshInstance : visibleMeshes)
    {
        if(meshInstance.GetMaterialId() != lastMaterialId)
        {
            pcMaterialChanges.Increment(1);

            lastMaterialId = meshInstance.GetMaterialId();

            const wgpu::BindGroup* bindGroup = propKit.GetMaterialBindGroup(lastMaterialId);
            MLG_ASSERT(bindGroup,
                "Failed to get material bind group for material ID {}",
                lastMaterialId.GetValue());

            renderPass.SetBindGroup(1, *bindGroup, 0, nullptr);
        }

        renderPass.DrawIndexed(meshInstance.GetIndexCount(),
            1,
            meshInstance.GetFirstIndex(),
            static_cast<int32_t>(meshInstance.GetBaseVertex()),
            static_cast<uint32_t>(meshInstance.GetInstanceIndex()));
    }

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