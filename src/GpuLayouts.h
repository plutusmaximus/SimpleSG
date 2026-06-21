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

    /// @brief Get or create a bind group layout for the specified type.
    /// @tparam T The type representing the bind group layout, typically a shader contract resource group.
    /// @param device The WebGPU device.
    /// @return The resulting bind group layout.
    template<typename T>
    static Result<wgpu::BindGroupLayout> GetOrCreateLayout(wgpu::Device device);

    /// @brief Create a bind group for the specified type using the provided resources.
    /// @tparam T The type representing the bind group layout, typically a shader contract resource group.
    /// @param device The WebGPU device.
    /// @param resources The resources to bind.
    /// @return The resulting bind group.
    template<typename T>
    static Result<wgpu::BindGroup> CreateBindGroup(wgpu::Device device,
        const T::Resources& resources);
};