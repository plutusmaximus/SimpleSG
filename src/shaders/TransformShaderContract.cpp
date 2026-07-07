#include "TransformShaderContract.h"

Result<wgpu::BindGroupLayout>
TransformShaderContract::SceneGroup::CreateLayout(const wgpu::Device& gpuDevice)
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

    return gpuDevice.CreateBindGroupLayout(&desc);
}

Result<wgpu::BindGroup>
TransformShaderContract::SceneGroup::CreateBindGroup(const wgpu::Device& gpuDevice, wgpu::BindGroupLayout layout,
    const SceneGroup::Resources& resources)
{
    MLG_CHECK(resources.Validate(),
        "Invalid resources provided for TransformShaderContract::SceneGroup::CreateBindGroup");

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
            .label = "TransformShaderSceneGroupBindings",
            .layout = layout,
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    return gpuDevice.CreateBindGroup(&desc);
}