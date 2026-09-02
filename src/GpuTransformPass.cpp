#define MLG_LOGGER_NAME "TPAS"

#include "GpuTransformPass.h"

#include "GpuHelper.h"

namespace
{
constexpr auto
CreateBindGroupLayoutEntries()
{
    return std::array//
    {
        // World transform.
        wgpu::BindGroupLayoutEntry//
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
        wgpu::BindGroupLayoutEntry//
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
        wgpu::BindGroupLayoutEntry{
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
}

auto
CreateBindGroupEntries(const GpuTransformPass::Inputs& inputs,
    const GpuTransformPass::Outputs& outputs)
{
    return std::array //
        {
            wgpu::BindGroupEntry //
            {
                .binding = 0,
                .buffer = inputs.WorldTransforms.GetGpuBuffer(),
                .offset = 0,
                .size = inputs.WorldTransforms.BufferSize(),
            },
            wgpu::BindGroupEntry //
            {
                .binding = 1,
                .buffer = outputs.ClipSpaceTransforms.GetGpuBuffer(),
                .offset = 0,
                .size = outputs.ClipSpaceTransforms.BufferSize(),
            },
            wgpu::BindGroupEntry //
            {
                .binding = 2,
                .buffer = inputs.CameraParams.GetGpuBuffer(),
                .offset = 0,
                .size = inputs.CameraParams.BufferSize(),
            },
        };
}

using LayoutEntries = decltype(CreateBindGroupLayoutEntries());

using BindGroupEntries =
    decltype(CreateBindGroupEntries(std::declval<const GpuTransformPass::Inputs&>(),
        std::declval<const GpuTransformPass::Outputs&>()));

static_assert(std::tuple_size_v<LayoutEntries> == std::tuple_size_v<BindGroupEntries>,
    "Bind group layout entries and bind group entries must have the same size");

Result<wgpu::BindGroupLayout>
CreateBindGroupLayout(const wgpu::Device& gpuDevice)
{
    auto bglEntries = CreateBindGroupLayoutEntries();

    const wgpu::BindGroupLayoutDescriptor desc //
        {
            .label = "GpuTransformPass",
            .entryCount = std::size(bglEntries),
            .entries = bglEntries.data(),
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

    const size_t instanceCount = m_Inputs->WorldTransforms.Count();

    return Invocation(m_GpuHelper->GetDevice(), std::move(computePass), instanceCount);
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

    auto entries = CreateBindGroupEntries(m_Inputs.value(), m_Outputs.value());

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = "GpuTransformPass",
            .layout = m_BindGroupLayout,
            .entryCount = std::size(entries),
            .entries = entries.data(),
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
GpuTransformPass::Invocation::Execute()
{
    MLG_CHECKV(m_ComputePass, "Pass has already been executed");

    // Consume the compute pass so it can't be used again.
    const wgpu::ComputePassEncoder computePass = std::move(m_ComputePass);

    m_ComputePass = {};

    // Number of workgroups to dispatch is the number of instances divided by the workgroup size,
    // rounded up.
    const size_t workgroupCountX = (m_InstanceCount / GpuTransformPass::kWorkgroupSize)
        + (m_InstanceCount % GpuTransformPass::kWorkgroupSize != 0);

    computePass.DispatchWorkgroups(static_cast<uint32_t>(workgroupCountX));
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