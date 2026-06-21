#pragma once

#include "GpuHelper.h"

class CompositeShaderContract
{
    static constexpr const char* ShaderPath = "shaders/CompositeShader.wgsl";
    static constexpr const char* VertexEntry = "vs_main";
    static constexpr const char* FragmentEntry = "fs_main";

public:

    struct MaterialGroup
    {
        static constexpr uint32_t GroupIndex = 0;

        struct Resources
        {
            wgpu::TextureView TextureView;
            wgpu::Sampler Sampler;

            bool Validate() const
            {
                return MLG_VERIFY(TextureView, "Invalid TextureView") &&
                       MLG_VERIFY(Sampler, "Invalid Sampler");
            }
        };

        static Result<wgpu::BindGroupLayout> CreateLayout(wgpu::Device device);

        static Result<wgpu::BindGroup> CreateBindGroup(wgpu::Device device, wgpu::BindGroupLayout layout,
            const Resources& resources);
    };

    static const char* GetShaderPath() { return ShaderPath; }

    static const char* GetVertexEntryPoint() { return VertexEntry; }

    static const char* GetFragmentEntryPoint() { return FragmentEntry; }
};