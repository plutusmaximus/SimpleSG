#include "GpuLayouts.h"

#include "shaders/ColorShaderContract.h"
#include "shaders/CompositorShaderContract.h"
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
GetOrCreateLayoutForType(const wgpu::Device& gpuDevice)
{
    static Layout s_Layout;

    if(s_Layout.GpuLayout)
    {
        return s_Layout.GpuLayout;
    }

    s_Layout.GpuLayout = T::CreateLayout(gpuDevice);

    s_Layout.Next = g_Layouts;
    g_Layouts = &s_Layout;

    return s_Layout.GpuLayout;
}
}

template<>
Result<wgpu::BindGroupLayout>
GpuLayouts::GetOrCreateLayout<ColorShaderContract::SceneGroup>(const wgpu::Device& gpuDevice)
{
    return GetOrCreateLayoutForType<ColorShaderContract::SceneGroup>(gpuDevice);
}

template<>
Result<wgpu::BindGroupLayout>
GpuLayouts::GetOrCreateLayout<ColorShaderContract::TextureGroup>(const wgpu::Device& gpuDevice)
{
    return GetOrCreateLayoutForType<ColorShaderContract::TextureGroup>(gpuDevice);
}

template<>
Result<wgpu::BindGroupLayout>
GpuLayouts::GetOrCreateLayout<CompositorShaderContract::TextureGroup>(const wgpu::Device& gpuDevice)
{
    return GetOrCreateLayoutForType<CompositorShaderContract::TextureGroup>(gpuDevice);
}

template<>
Result<wgpu::BindGroupLayout>
GpuLayouts::GetOrCreateLayout<TransformShaderContract::SceneGroup>(const wgpu::Device& gpuDevice)
{
    return GetOrCreateLayoutForType<TransformShaderContract::SceneGroup>(gpuDevice);
}

template<>
Result<wgpu::BindGroup>
GpuLayouts::CreateBindGroup<ColorShaderContract::SceneGroup>(const wgpu::Device& gpuDevice,
    const ColorShaderContract::SceneGroup::Resources& resources)
{
    auto layout = GetOrCreateLayoutForType<ColorShaderContract::SceneGroup>(gpuDevice);
    MLG_CHECK(layout);

    return ColorShaderContract::SceneGroup::CreateBindGroup(gpuDevice, *layout, resources);
}

template<>
Result<wgpu::BindGroup>
GpuLayouts::CreateBindGroup<ColorShaderContract::TextureGroup>(const wgpu::Device& gpuDevice,
    const ColorShaderContract::TextureGroup::Resources& resources)
{
    auto layout = GetOrCreateLayoutForType<ColorShaderContract::TextureGroup>(gpuDevice);
    MLG_CHECK(layout);

    return ColorShaderContract::TextureGroup::CreateBindGroup(gpuDevice, *layout, resources);
}

template<>
Result<wgpu::BindGroup>
GpuLayouts::CreateBindGroup<CompositorShaderContract::TextureGroup>(const wgpu::Device& gpuDevice,
    const CompositorShaderContract::TextureGroup::Resources& resources)
{
    auto layout = GetOrCreateLayoutForType<CompositorShaderContract::TextureGroup>(gpuDevice);
    MLG_CHECK(layout);

    return CompositorShaderContract::TextureGroup::CreateBindGroup(gpuDevice, *layout, resources);
}

template<>
Result<wgpu::BindGroup>
GpuLayouts::CreateBindGroup<TransformShaderContract::SceneGroup>(const wgpu::Device& gpuDevice,
    const TransformShaderContract::SceneGroup::Resources& resources)
{
    auto layout = GetOrCreateLayoutForType<TransformShaderContract::SceneGroup>(gpuDevice);
    MLG_CHECK(layout);

    return TransformShaderContract::SceneGroup::CreateBindGroup(gpuDevice, *layout, resources);
}