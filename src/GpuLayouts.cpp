#include "GpuLayouts.h"

#include "shaders/ColorShaderContract.h"
#include "shaders/CompositeShaderContract.h"
#include "shaders/TransformShaderContract.h"

namespace
{
struct Layout
{
    Result<wgpu::BindGroupLayout> GpuLayout;
    struct Layout* Next{ nullptr };
};

Layout* g_Layouts = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

template<typename T>
Result<wgpu::BindGroupLayout>
GetOrCreateLayoutForType(wgpu::Device device)
{
    static Layout s_Layout;

    if(s_Layout.GpuLayout)
    {
        return s_Layout.GpuLayout;
    }

    s_Layout.GpuLayout = T::CreateLayout(device);

    s_Layout.Next = g_Layouts;
    g_Layouts = &s_Layout;

    return s_Layout.GpuLayout;
}
}

template<>
Result<wgpu::BindGroupLayout>
GpuLayouts::GetOrCreateLayout<ColorShaderContract::SceneGroup>(wgpu::Device device)
{
    return GetOrCreateLayoutForType<ColorShaderContract::SceneGroup>(device);
}

template<>
Result<wgpu::BindGroupLayout>
GpuLayouts::GetOrCreateLayout<ColorShaderContract::TextureGroup>(wgpu::Device device)
{
    return GetOrCreateLayoutForType<ColorShaderContract::TextureGroup>(device);
}

template<>
Result<wgpu::BindGroupLayout>
GpuLayouts::GetOrCreateLayout<CompositeShaderContract::TextureGroup>(wgpu::Device device)
{
    return GetOrCreateLayoutForType<CompositeShaderContract::TextureGroup>(device);
}

template<>
Result<wgpu::BindGroupLayout>
GpuLayouts::GetOrCreateLayout<TransformShaderContract::SceneGroup>(wgpu::Device device)
{
    return GetOrCreateLayoutForType<TransformShaderContract::SceneGroup>(device);
}

template<>
Result<wgpu::BindGroup>
GpuLayouts::CreateBindGroup<ColorShaderContract::SceneGroup>(wgpu::Device device,
    const ColorShaderContract::SceneGroup::Resources& resources)
{
    auto layout = GetOrCreateLayoutForType<ColorShaderContract::SceneGroup>(device);
    MLG_CHECK(layout);

    return ColorShaderContract::SceneGroup::CreateBindGroup(device, *layout, resources);
}

template<>
Result<wgpu::BindGroup>
GpuLayouts::CreateBindGroup<ColorShaderContract::TextureGroup>(wgpu::Device device,
    const ColorShaderContract::TextureGroup::Resources& resources)
{
    auto layout = GetOrCreateLayoutForType<ColorShaderContract::TextureGroup>(device);
    MLG_CHECK(layout);

    return ColorShaderContract::TextureGroup::CreateBindGroup(device, *layout, resources);
}

template<>
Result<wgpu::BindGroup>
GpuLayouts::CreateBindGroup<CompositeShaderContract::TextureGroup>(wgpu::Device device,
    const CompositeShaderContract::TextureGroup::Resources& resources)
{
    auto layout = GetOrCreateLayoutForType<CompositeShaderContract::TextureGroup>(device);
    MLG_CHECK(layout);

    return CompositeShaderContract::TextureGroup::CreateBindGroup(device, *layout, resources);
}

template<>
Result<wgpu::BindGroup>
GpuLayouts::CreateBindGroup<TransformShaderContract::SceneGroup>(wgpu::Device device,
    const TransformShaderContract::SceneGroup::Resources& resources)
{
    auto layout = GetOrCreateLayoutForType<TransformShaderContract::SceneGroup>(device);
    MLG_CHECK(layout);

    return TransformShaderContract::SceneGroup::CreateBindGroup(device, *layout, resources);
}