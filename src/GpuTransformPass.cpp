#define MLG_LOGGER_NAME "TPAS"

#include "GpuTransformPass.h"

#include "GpuHelper.h"

Result<GpuTransformPass>
GpuTransformPass::Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
{
    auto shader = gpuHelper.LoadShader(ShaderPath, fileFetcher);
    MLG_CHECK(shader);

    GpuTransformPass pass(gpuHelper, std::move(*shader));

    MLG_CHECK(pass.EnsurePipeline());

    return pass;
}

Result<>
GpuTransformPass::SetInputs(const Inputs& inputs)
{
    if(m_Inputs && *m_Inputs == inputs)
    {
        // Inputs are the same, no need to update
        return Result<>::Ok;
    }

    m_Inputs = inputs;

    m_BindGroup = {};

    if(m_Inputs && m_Outputs)
    {
        MLG_CHECK(EnsureBindgroup());
    }

    return Result<>::Ok;
}

Result<>
GpuTransformPass::SetOutputs(const Outputs& outputs)
{
    if(m_Outputs && *m_Outputs == outputs)
    {
        // Outputs are the same, no need to update
        return Result<>::Ok;
    }

    m_Outputs = outputs;

    m_BindGroup = {};

    if(m_Inputs && m_Outputs)
    {
        MLG_CHECK(EnsureBindgroup());
    }

    return Result<>::Ok;
}

Result<wgpu::ComputePassEncoder>
GpuTransformPass::BeginPass(const wgpu::CommandEncoder& cmdEncoder) const
{
    MLG_CHECKV(m_Pipeline, "Pipeline is not valid");
    MLG_CHECKV(m_BindGroup, "Bind group is not valid - forget to call SetInputs() and SetOutputs()?");

    const wgpu::ComputePassEncoder computePass = cmdEncoder.BeginComputePass();
    computePass.SetPipeline(m_Pipeline);
    computePass.SetBindGroup(0, m_BindGroup);

    return computePass;
}

// private:

Result<>
GpuTransformPass::EnsureBindgroup()
{
    MLG_CHECKV(m_Inputs, "Inputs are not valid - forget to call SetInputs()?");
    MLG_CHECKV(m_Outputs, "Outputs are not valid - forget to call SetOutputs()?");
    MLG_CHECKV(m_BindGroupLayout, "Bind group layout is not valid");

    if(!m_BindGroup)
    {
        const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

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

        m_BindGroup = gpuDevice.CreateBindGroup(&desc);
        MLG_CHECKV(m_BindGroup, "Failed to create bind group");
    }

    return Result<>::Ok;
}

Result<>
GpuTransformPass::EnsurePipeline()
{
    if(m_Pipeline)
    {
        return Result<>::Ok;
    }

    const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

    if(!m_BindGroupLayout)
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

        m_BindGroupLayout = gpuDevice.CreateBindGroupLayout(&desc);
        MLG_CHECK(m_BindGroupLayout, "Failed to create bind group layout");
    }

    if(!m_PipelineLayout)
    {
        const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
            {
                .label = "GpuTransformPass",
                .bindGroupLayoutCount = 1,
                .bindGroupLayouts = &m_BindGroupLayout,
            };

        m_PipelineLayout = gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
        MLG_CHECK(m_PipelineLayout, "Failed to create pipeline layout");
    }

    const wgpu::ComputePipelineDescriptor desc //
        {
            .label = "GpuTransformPass",
            .layout = m_PipelineLayout,
            .compute //
            {
                .module = *m_Shader,
                .entryPoint = ComputeEntry,
            },
        };
    ;

    m_Pipeline = gpuDevice.CreateComputePipeline(&desc);
    MLG_CHECK(m_Pipeline, "Failed to create pipeline");

    return Result<>::Ok;
}
