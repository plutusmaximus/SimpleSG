#define MLG_LOGGER_NAME "TPAS"

#include "GpuTransformPass.h"

#include "FileFetcher.h"
#include "GpuHelper.h"

#include <thread>
#include <webgpu/webgpu_cpp.h>

namespace
{

Result<>
LoadShaderCode(const char* filePath, std::vector<uint8_t>& outBuffer, FileFetcher& fileFetcher)
{
    FileFetcher::Request request(filePath);
    MLG_CHECK(fileFetcher.Fetch(request));

    while(request.IsPending())
    {
        MLG_CHECK(fileFetcher.ProcessCompletions());
        std::this_thread::yield();
    }

    MLG_CHECK(request.Succeeded(), "Failed to load shader file: {}", filePath);

    request.MoveDataTo(outBuffer);

    return Result<>::Ok;
}

Result<wgpu::ShaderModule>
CreateShader(const wgpu::Device& gpuDevice, const std::vector<uint8_t>& shaderCode)
{
    const void* data = shaderCode.data();
    const wgpu::StringView shaderCodeView{ static_cast<const char*>(data), shaderCode.size() };
    const wgpu::ShaderSourceWGSL wgsl{ { .nextInChain = nullptr, .code = shaderCodeView } };
    const wgpu::ShaderModuleDescriptor shaderModuleDescriptor //
        { .nextInChain = &wgsl, .label = "CompositorShader" };

    wgpu::ShaderModule shaderModule = gpuDevice.CreateShaderModule(&shaderModuleDescriptor);
    MLG_CHECK(shaderModule, "Failed to create shader module");

    return shaderModule;
}

} // namespace

Result<GpuTransformPass>
GpuTransformPass::Create(const GpuHelper& /*gpuHelper*/, FileFetcher& fileFetcher)
{
    GpuTransformPass pass;

    std::vector<uint8_t> shaderCode;
    MLG_CHECK(LoadShaderCode(ShaderPath, shaderCode, fileFetcher));

    pass.m_ShaderCode = std::move(shaderCode);

    return pass;
}

Result<>
GpuTransformPass::BindResources(const GpuHelper& gpuHelper, const Resources& resources)
{
    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

    MLG_CHECK(EnsurePipeline(gpuDevice));

    MLG_CHECKV(resources.Validate());
    MLG_CHECKV(m_PipelineResources.BindGroupLayout, "Pipeline bind group layout is not valid");

    if(!m_BindGroup)
    {
        const wgpu::BindGroupEntry entries[] = //
            {
                {
                    .binding = 0,
                    .buffer = resources.WorldTransforms.GetGpuBuffer(),
                    .offset = 0,
                    .size = resources.WorldTransforms.BufferSize(),
                },
                {
                    .binding = 1,
                    .buffer = resources.ClipSpaceTransforms.GetGpuBuffer(),
                    .offset = 0,
                    .size = resources.ClipSpaceTransforms.BufferSize(),
                },
                {
                    .binding = 2,
                    .buffer = resources.CameraParams.GetGpuBuffer(),
                    .offset = 0,
                    .size = resources.CameraParams.BufferSize(),
                },
            };

        const wgpu::BindGroupDescriptor desc = //
            {
                .label = "Transform",
                .layout = m_PipelineResources.BindGroupLayout,
                .entryCount = std::size(entries),
                .entries = &entries[0],
            };

        m_BindGroup = gpuDevice.CreateBindGroup(&desc);
        MLG_CHECKV(m_BindGroup, "Failed to create bind group");
    }

    m_Resources = resources;

    return Result<>::Ok;
}

Result<wgpu::ComputePassEncoder>
GpuTransformPass::BeginComputePass(const wgpu::CommandEncoder& cmdEncoder) const
{
    MLG_CHECKV(m_Pipeline, "Pipeline is not valid - forget to call BindResources()?");
    MLG_CHECKV(m_BindGroup, "Bind group is not valid - forget to call BindResources()?");

    const wgpu::ComputePassEncoder computePass = cmdEncoder.BeginComputePass();
    computePass.SetPipeline(m_Pipeline);
    computePass.SetBindGroup(0, m_BindGroup);

    return computePass;
}

// private:

Result<>
GpuTransformPass::EnsurePipeline(const wgpu::Device& gpuDevice)
{
    if(m_Pipeline)
    {
        return Result<>::Ok;
    }

    if(!m_PipelineResources.Shader)
    {
        auto shader = CreateShader(gpuDevice, m_ShaderCode);
        MLG_CHECK(shader);

        m_PipelineResources.Shader = std::move(*shader);
    }

    if(!m_PipelineResources.BindGroupLayout)
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
                .label = "TransformShaderSceneGroupLayout",
                .entryCount = std::size(entries),
                .entries = &entries[0],
            };

        m_PipelineResources.BindGroupLayout = gpuDevice.CreateBindGroupLayout(&desc);
        MLG_CHECK(m_PipelineResources.BindGroupLayout, "Failed to create bind group layout");
    }

    if(!m_PipelineResources.PipelineLayout)
    {
        const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
            {
                .label = "Transform",
                .bindGroupLayoutCount = 1,
                .bindGroupLayouts = &m_PipelineResources.BindGroupLayout,
            };

        m_PipelineResources.PipelineLayout = gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
        MLG_CHECK(m_PipelineResources.PipelineLayout, "Failed to create pipeline layout");
    }

    const wgpu::ComputePipelineDescriptor desc//
    {
        .layout = m_PipelineResources.PipelineLayout,
        .compute//
        {
            .module = m_PipelineResources.Shader,
            .entryPoint = ComputeEntry,
        },
    };;

    m_Pipeline = gpuDevice.CreateComputePipeline(&desc);
    MLG_CHECK(m_Pipeline, "Failed to create pipeline");

    return Result<>::Ok;
}
