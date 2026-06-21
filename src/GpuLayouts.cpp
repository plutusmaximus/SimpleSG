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
GpuLayouts::GetOrCreateLayout<ColorShaderContract::MaterialGroup>(wgpu::Device device)
{
    return GetOrCreateLayoutForType<ColorShaderContract::MaterialGroup>(device);
}

template<>
Result<wgpu::BindGroupLayout>
GpuLayouts::GetOrCreateLayout<CompositeShaderContract::MaterialGroup>(wgpu::Device device)
{
    return GetOrCreateLayoutForType<CompositeShaderContract::MaterialGroup>(device);
}

template<>
Result<wgpu::BindGroupLayout>
GpuLayouts::GetOrCreateLayout<TransformShaderContract::SceneGroup>(wgpu::Device device)
{
    return GetOrCreateLayoutForType<TransformShaderContract::SceneGroup>(device);
}