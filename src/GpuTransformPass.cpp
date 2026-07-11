#define MLG_LOGGER_NAME "TPAS"

#include "GpuTransformPass.h"

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

Result<GpuTransformPass>
GpuTransformPass::Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
{
    GpuTransformPass pass;

    auto shader = LoadShader(ShaderPath, gpuHelper.GetDevice(), fileFetcher);
    MLG_CHECK(shader);

    pass.m_Shader = std::move(*shader);

    MLG_CHECK(pass.EnsurePipeline(gpuHelper.GetDevice()));

    return pass;
}

Result<>
GpuTransformPass::BindInputs(const GpuHelper& gpuHelper, const Inputs& inputs)
{
    MLG_CHECKV(inputs.Validate());

    m_BindGroup = {};

    m_Inputs = inputs;

    if(m_Inputs && m_Outputs)
    {
        MLG_CHECK(EnsureBindgroup(gpuHelper.GetDevice()));
    }

    return Result<>::Ok;
}

Result<>
GpuTransformPass::BindOutputs(const GpuHelper& gpuHelper, const Outputs& outputs)
{
    MLG_CHECKV(outputs.Validate());

    m_BindGroup = {};

    m_Outputs = outputs;

    if(m_Inputs && m_Outputs)
    {
        MLG_CHECK(EnsureBindgroup(gpuHelper.GetDevice()));
    }

    return Result<>::Ok;
}

Result<wgpu::ComputePassEncoder>
GpuTransformPass::BeginComputePass(const wgpu::CommandEncoder& cmdEncoder) const
{
    MLG_CHECKV(m_Pipeline, "Pipeline is not valid");
    MLG_CHECKV(m_BindGroup, "Bind group is not valid - forget to call BindInputs() and BindOutputs()?");

    const wgpu::ComputePassEncoder computePass = cmdEncoder.BeginComputePass();
    computePass.SetPipeline(m_Pipeline);
    computePass.SetBindGroup(0, m_BindGroup);

    return computePass;
}

// private:

Result<>
GpuTransformPass::EnsureBindgroup(const wgpu::Device& gpuDevice)
{
    MLG_CHECKV(m_Inputs, "Inputs are not valid - forget to call BindInputs()?");
    MLG_CHECKV(m_Outputs, "Outputs are not valid - forget to call BindOutputs()?");
    MLG_CHECKV(m_BindGroupLayout, "Bind group layout is not valid");
    MLG_CHECKV(m_Inputs->Validate());
    MLG_CHECKV(m_Outputs->Validate());

    if(!m_BindGroup)
    {
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
GpuTransformPass::EnsurePipeline(const wgpu::Device& gpuDevice)
{
    if(m_Pipeline)
    {
        return Result<>::Ok;
    }

    MLG_CHECKV(m_Shader, "Shader is not valid");

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
                .module = m_Shader,
                .entryPoint = ComputeEntry,
            },
        };
    ;

    m_Pipeline = gpuDevice.CreateComputePipeline(&desc);
    MLG_CHECK(m_Pipeline, "Failed to create pipeline");

    return Result<>::Ok;
}
