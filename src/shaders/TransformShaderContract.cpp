#include "TransformShaderContract.h"

wgpu::BindGroupLayout
TransformShaderContract::SceneGroup::CreateLayout(wgpu::Device device)
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

    return device.CreateBindGroupLayout(&desc);
}

wgpu::BindGroup
TransformShaderContract::SceneGroup::CreateBindGroup(wgpu::Device device, wgpu::BindGroupLayout layout,
    const SceneGroup::Resources& resources)
{
    if(!resources.Validate())
    {
        MLG_ERROR("Invalid resources provided for TransformShaderContract::SceneGroup::CreateBindGroup");
        return nullptr;
    }
    
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

    return device.CreateBindGroup(&desc);
}