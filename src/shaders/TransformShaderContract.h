#pragma once

#include "GpuBufferTypes.h"

class TransformShaderContract
{
    static constexpr const char* ShaderPath = "shaders/TransformShader.wgsl";
    static constexpr const char* Entry = "main";

public:

    struct SceneGroup
    {
        static constexpr uint32_t GroupIndex = 0;

        struct Resources
        {
            WorldTransformBuffer WorldTransforms;
            ClipSpaceBuffer ClipSpaceTransforms;
            CameraParamsBuffer CameraParams;
        };

        static wgpu::BindGroupLayout CreateLayout(wgpu::Device device);

        static wgpu::BindGroup CreateBindGroup(wgpu::Device device, wgpu::BindGroupLayout layout,
            const Resources& resources);
    };

    static const char* GetShaderPath() { return ShaderPath; }

    static const char* GetEntryPoint() { return Entry; }
};