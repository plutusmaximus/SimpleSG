#include "CompositeShaderContract.h"

Result<wgpu::BindGroupLayout>
CompositeShaderContract::MaterialGroup::CreateLayout(wgpu::Device device)
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
            .label = "CompositeShaderMaterialGroupLayout",
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    return device.CreateBindGroupLayout(&desc);
}

Result<wgpu::BindGroup>
CompositeShaderContract::MaterialGroup::CreateBindGroup(wgpu::Device device, wgpu::BindGroupLayout layout,
    const MaterialGroup::Resources& resources)
{
    MLG_CHECK(resources.Validate(),
        "Invalid resources provided for CompositeShaderContract::MaterialGroup::CreateBindGroup");
        
    const wgpu::BindGroupEntry entries[] = //
        {
            {
                .binding = 0,
                .textureView = resources.TextureView,
            },
            {
                .binding = 1,
                .sampler = resources.Sampler,
            },
        };

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = "CompositeShaderMaterialGroupBindings",
            .layout = layout,
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    return device.CreateBindGroup(&desc);
}