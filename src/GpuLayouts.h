#pragma once

#include "Result.h"

#include <webgpu/webgpu_cpp.h>

class GpuLayouts final
{
public:

    GpuLayouts() = delete;
    ~GpuLayouts() = delete;
    GpuLayouts(const GpuLayouts&) = delete;
    GpuLayouts& operator=(const GpuLayouts&) = delete;
    GpuLayouts(GpuLayouts&&) = delete;
    GpuLayouts& operator=(GpuLayouts&&) = delete;
    
    template<typename T>
    static Result<wgpu::BindGroupLayout> GetOrCreateLayout(wgpu::Device device);
};