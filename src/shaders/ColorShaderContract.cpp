#include "ColorShaderContract.h"

Result<wgpu::BindGroupLayout>
ColorShaderContract::TextureGroup::CreateLayout(const wgpu::Device& gpuDevice)
{
    const wgpu::BindGroupLayoutEntry entries[] =//
    {
        // Texture
        {
            .binding = 0,
            .visibility = wgpu::ShaderStage::Fragment,
            .texture =
            {
                .sampleType = wgpu::TextureSampleType::Float,
                .viewDimension = wgpu::TextureViewDimension::e2D,
                .multisampled = false,
            },
        },
        // Sampler
        {
            .binding = 1,
            .visibility = wgpu::ShaderStage::Fragment,
            .sampler =
            {
                .type = wgpu::SamplerBindingType::Filtering,
            },
        },
    };

    const wgpu::BindGroupLayoutDescriptor desc = //
        {
            .label = "ColorShaderTextureGroupLayout",
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    return gpuDevice.CreateBindGroupLayout(&desc);
}

Result<wgpu::BindGroup>
ColorShaderContract::TextureGroup::CreateBindGroup(const wgpu::Device& gpuDevice, wgpu::BindGroupLayout layout,
    const TextureGroup::Resources& resources)
{
    MLG_CHECK(resources.Validate(),
        "Invalid resources provided for ColorShaderContract::TextureGroup::CreateBindGroup");

    const wgpu::BindGroupEntry entries[] = //
        {
            {
                .binding = 0,
                .textureView = resources.BaseTexture.CreateView(),
            },
            {
                .binding = 1,
                .sampler = resources.BaseSampler,
            },
        };

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = "ColorShaderTextureGroupBindings",
            .layout = layout,
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    return gpuDevice.CreateBindGroup(&desc);
}