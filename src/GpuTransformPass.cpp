#include <webgpu/webgpu_cpp.h>
#define MLG_LOGGER_NAME "TPAS"

#include "GpuHelper.h"
#include "GpuTransformPass.h"

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
    if(outputs != m_Outputs)
    {
        m_Outputs = outputs;

        // Rebuild the bind group
        m_InputOutputBindGroup = {};
    }

    return Result<>::Ok;
}

Result<wgpu::ComputePassEncoder>
GpuTransformPass::BeginPass(const wgpu::CommandEncoder& cmdEncoder)
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

    return computePass;
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