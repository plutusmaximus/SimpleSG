#pragma once

#include "Result.h"

#include <webgpu/webgpu_cpp.h>

class ColorShaderContract
{
public:

    struct TextureGroup
    {
        static constexpr uint32_t GroupIndex = 1;

        struct Resources
        {
            wgpu::Texture BaseTexture;
            wgpu::Sampler BaseSampler;

            bool Validate() const
            {
                return MLG_VERIFY(BaseTexture, "Invalid BaseTexture") &&
                       MLG_VERIFY(BaseSampler, "Invalid BaseSampler");
            }
        };

        static Result<wgpu::BindGroupLayout> CreateLayout(const wgpu::Device& gpuDevice);

        static Result<wgpu::BindGroup> CreateBindGroup(const wgpu::Device& gpuDevice, wgpu::BindGroupLayout layout,
            const Resources& resources);
    };
};