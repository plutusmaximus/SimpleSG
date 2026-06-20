#include "ColorShaderContract.h"

wgpu::BindGroupLayout
ColorShaderContract::SceneGroup::CreateLayout(wgpu::Device device)
{
    const wgpu::BindGroupLayoutEntry entries[]//
    {
        // World transform.
        {
            .binding = 0,
            .visibility = wgpu::ShaderStage::Vertex,
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
            .visibility = wgpu::ShaderStage::Vertex,
            .buffer =
            {
                .type = wgpu::BufferBindingType::ReadOnlyStorage,
                .hasDynamicOffset = false,
                .minBindingSize = sizeof(ShaderInterop::ClipSpaceTransform),
            },
        },
        // Mesh properties.
        {
            .binding = 2,
            .visibility = wgpu::ShaderStage::Vertex | wgpu::ShaderStage::Fragment,
            .buffer =
            {
                .type = wgpu::BufferBindingType::ReadOnlyStorage,
                .hasDynamicOffset = false,
                .minBindingSize = sizeof(ShaderInterop::MeshProperties),
            },
        },
        // Material constants buffer.
        {
            .binding = 3,
            .visibility = wgpu::ShaderStage::Fragment,
            .buffer =
            {
                .type = wgpu::BufferBindingType::ReadOnlyStorage,
                .hasDynamicOffset = false,
                .minBindingSize = sizeof(ShaderInterop::MaterialConstants),
            },
        },
        // Camera parameters
        {
            .binding = 4,
            .visibility = wgpu::ShaderStage::Vertex,
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
            .label = "ColorShaderSceneGroupLayout",
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    return device.CreateBindGroupLayout(&desc);
}

wgpu::BindGroup
ColorShaderContract::SceneGroup::CreateBindGroup(wgpu::Device device, wgpu::BindGroupLayout layout,
    const SceneGroup::Resources& resources)
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
                .buffer = resources.MeshProperties.GetGpuBuffer(),
                .offset = 0,
                .size = resources.MeshProperties.BufferSize(),
            },
            {
                .binding = 3,
                .buffer = resources.MaterialConstants.GetGpuBuffer(),
                .offset = 0,
                .size = resources.MaterialConstants.BufferSize(),
            },
            {
                .binding = 4,
                .buffer = resources.CameraParams.GetGpuBuffer(),
                .offset = 0,
                .size = resources.CameraParams.BufferSize(),
            },
        };

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = "ColorShaderSceneGroupBindings",
            .layout = layout,
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    return device.CreateBindGroup(&desc);
}

wgpu::BindGroupLayout
ColorShaderContract::MaterialGroup::CreateLayout(wgpu::Device device)
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
            .label = "ColorShaderMaterialGroupLayout",
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    return device.CreateBindGroupLayout(&desc);
}

wgpu::BindGroup
ColorShaderContract::MaterialGroup::CreateBindGroup(wgpu::Device device, wgpu::BindGroupLayout layout,
    const MaterialGroup::Resources& resources)
{
    const wgpu::BindGroupEntry entries[] = //
        {
            {
                .binding = 0,
                .textureView = resources.BaseTexture.GetGpuTexture().CreateView(),
            },
            {
                .binding = 1,
                .sampler = resources.BaseSampler,
            },
        };

    const wgpu::BindGroupDescriptor desc = //
        {
            .label = "ColorShaderMaterialGroupBindings",
            .layout = layout,
            .entryCount = std::size(entries),
            .entries = &entries[0],
        };

    return device.CreateBindGroup(&desc);
}

wgpu::VertexBufferLayout
ColorShaderContract::GetVertexBufferLayout()
{
    static const wgpu::VertexAttribute attributes[] = //
        {
            {
                .format = wgpu::VertexFormat::Float32x3,
                .offset = offsetof(Vertex, pos),
                .shaderLocation = 0,
            },
            {
                .format = wgpu::VertexFormat::Float32x3,
                .offset = offsetof(Vertex, normal),
                .shaderLocation = 1,
            },
            {
                .format = wgpu::VertexFormat::Float32x2,
                .offset = offsetof(Vertex, uvs[0]),
                .shaderLocation = 2,
            },
        };

    static const wgpu::VertexBufferLayout layout = //
        {
            .stepMode = wgpu::VertexStepMode::Vertex,
            .arrayStride = sizeof(Vertex),
            .attributeCount = std::size(attributes),
            .attributes = &attributes[0],
        };

    return layout;
}