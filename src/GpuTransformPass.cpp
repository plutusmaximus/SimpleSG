#define MLG_LOGGER_NAME "TPAS"

#include "GpuTransformPass.h"

#include "GpuHelper.h"

namespace
{
Result<wgpu::BindGroupLayout>
CreateBindGroupLayout(const wgpu::Device& gpuDevice)
{
    const wgpu::BindGroupLayoutEntry entries[]//
        {
            // World transform.
            {
                .binding = 0,
                .visibility = wgpu::ShaderStage::Compute,
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
                .visibility = wgpu::ShaderStage::Compute,
                .buffer =
                {
                    .type = wgpu::BufferBindingType::Storage,
                    .hasDynamicOffset = false,
                    .minBindingSize = sizeof(ShaderInterop::ClipSpaceTransform),
                },
            },
            // Camera parameters
            {
                .binding = 2,
                .visibility = wgpu::ShaderStage::Compute,
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
            .label = "GpuTransformPass",
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    wgpu::BindGroupLayout layout = gpuDevice.CreateBindGroupLayout(&desc);
    MLG_CHECK(layout, "Failed to create bind group layout");

    return layout;
}

Result<wgpu::PipelineLayout>
CreatePipelineLayout(const wgpu::Device& gpuDevice, const wgpu::BindGroupLayout& bindGroupLayout)
{
    MLG_CHECKV(bindGroupLayout, "Bind group layout is not valid");

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "GpuTransformPass",
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &bindGroupLayout,
        };

    wgpu::PipelineLayout pipelineLayout = gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(pipelineLayout, "Failed to create pipeline layout");
    return pipelineLayout;
}

} // namespace

Result<GpuTransformPass>
GpuTransformPass::Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
{
    auto shader = gpuHelper.LoadShader(ShaderPath, fileFetcher);
    MLG_CHECK(shader, "Failed to load shader: {}", ShaderPath);

    auto bindGroupLayout = CreateBindGroupLayout(gpuHelper.GetDevice());
    MLG_CHECK(bindGroupLayout);

    auto pipelineLayout = CreatePipelineLayout(gpuHelper.GetDevice(), *bindGroupLayout);
    MLG_CHECK(pipelineLayout);

    GpuTransformPass pass(gpuHelper, *shader, *bindGroupLayout, *pipelineLayout);

    return pass;
}

Result<>
GpuTransformPass::SetInputs(const Inputs& inputs)
{
    MLG_CHECK(inputs.Validate(), "Inputs are not valid");

    if(inputs != m_Inputs)
    {
        m_Inputs = inputs;

        // Rebuild the bind group
        m_InputOutputBindGroup = {};
    }

    return Result<>::Ok;
}

Result<>
GpuTransformPass::SetOutputs(const Outputs& outputs)
{
    MLG_CHECK(outputs.Validate(), "Outputs are not valid");

    if(outputs != m_Outputs)
    {
        m_Outputs = outputs;

        // Rebuild the bind group
        m_InputOutputBindGroup = {};
    }

    return Result<>::Ok;
}

Result<GpuTransformPass::Invocation>
GpuTransformPass::Prepare()
{
    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "GpuTransformPass" };
    const wgpu::CommandEncoder cmdEncoder =
        m_GpuHelper->GetDevice().CreateCommandEncoder(&encoderDesc);
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

Result<GpuTransformPass::Invocation>
GpuTransformPass::Prepare(wgpu::CommandEncoder cmdEncoder)
{
    MLG_CHECK(EnsurePipeline());
    MLG_CHECK(EnsureInputOutputBindGroup());

    MLG_CHECKV(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");
    MLG_CHECKV(m_Outputs, "Outputs are not valid - forget to call SetOutputs()?");

    MLG_CHECK(m_Inputs->WorldTransforms.BufferSize() <= m_Outputs->ClipSpaceTransforms.BufferSize(),
        "The ClipSpaceTransforms buffer must be at least as big as the WorldTransforms buffer");

    const wgpu::ComputePassEncoder computePass = cmdEncoder.BeginComputePass();
    MLG_CHECK(computePass, "Failed to begin compute pass");

    computePass.SetPipeline(m_Pipeline);
    computePass.SetBindGroup(0, m_InputOutputBindGroup);

    return Invocation(m_GpuHelper->GetDevice(), std::move(computePass));
}

// private:

Result<>
GpuTransformPass::EnsurePipeline()
{
    if(m_Pipeline)
    {
        return Result<>::Ok;
    }

    const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();
    const wgpu::ConstantEntry constants[] //
        {
            {
                .key = kWorkgroupSizeOverride,
                .value = static_cast<double>(kWorkgroupSize),
            },
        };

    const wgpu::ComputePipelineDescriptor desc //
        {
            .label = "GpuTransformPass",
            .layout = m_PipelineLayout,
            .compute //
            {
                .module = m_Shader,
                .entryPoint = ComputeEntry,
                .constantCount = std::size(constants),
                .constants = &constants[0],
            },
        };
    ;

    m_Pipeline = gpuDevice.CreateComputePipeline(&desc);
    MLG_CHECK(m_Pipeline, "Failed to create pipeline");

    return Result<>::Ok;
}

Result<>
GpuTransformPass::EnsureInputOutputBindGroup()
{
    if(m_InputOutputBindGroup)
    {
        return Result<>::Ok;
    }

    MLG_CHECKV(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");
    MLG_CHECKV(m_Outputs, "Outputs are not valid - forget to call SetOutputs()?");

    const wgpu::BindGroupEntry entries[] = //
        {
            {
                .binding = 0,
                .buffer = m_Inputs->WorldTransforms.GetGpuBuffer(),
                .offset = 0,
                .size = m_Inputs->WorldTransforms.BufferSize(),
            },
            {
                .binding = 1,
                .buffer = m_Outputs->ClipSpaceTransforms.GetGpuBuffer(),
                .offset = 0,
                .size = m_Outputs->ClipSpaceTransforms.BufferSize(),
            },
            {
                .binding = 2,
                .buffer = m_Inputs->CameraParams.GetGpuBuffer(),
                .offset = 0,
                .size = m_Inputs->CameraParams.BufferSize(),
            },
        };

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = "GpuTransformPass",
            .layout = m_BindGroupLayout,
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    m_InputOutputBindGroup = m_GpuHelper->GetDevice().CreateBindGroup(&desc);
    MLG_CHECKV(m_InputOutputBindGroup, "Failed to create bind group");

    return Result<>::Ok;
}

// GpuTransformPass::Invocation

GpuTransformPass::Invocation::~Invocation()
{
    MLG_ASSERT(!m_ComputePass, "Pass must be executed before destruction");
}

Result<>
GpuTransformPass::Invocation::Execute(const size_t instanceCount)
{
    MLG_CHECKV(m_ComputePass, "Pass has already been executed");

    // Consume the compute pass so it can't be used again.
    const wgpu::ComputePassEncoder computePass = std::move(m_ComputePass);

    m_ComputePass = {};

    // Number of workgroups to dispatch is the number of instances divided by the workgroup size,
    // rounded up.
    const size_t workgroupCountX = (instanceCount / GpuTransformPass::kWorkgroupSize)
        + (instanceCount % GpuTransformPass::kWorkgroupSize != 0);

    computePass.DispatchWorkgroups(narrow_cast<uint32_t>(workgroupCountX));
    computePass.End();

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