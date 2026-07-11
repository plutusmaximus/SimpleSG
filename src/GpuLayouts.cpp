#include "GpuLayouts.h"

#include "shaders/ColorShaderContract.h"

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
GpuLayouts::GetOrCreateLayout<ColorShaderContract::TextureGroup>(const wgpu::Device& gpuDevice)
{
    return GetOrCreateLayoutForType<ColorShaderContract::TextureGroup>(gpuDevice);
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