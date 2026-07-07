#pragma once

#include "GpuBufferTypes.h"
#include "Result.h"

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

            bool Validate() const
            {
                return MLG_VERIFY(WorldTransforms, "Invalid WorldTransforms buffer") &&
                       MLG_VERIFY(ClipSpaceTransforms, "Invalid ClipSpaceTransforms buffer") &&
                       MLG_VERIFY(CameraParams, "Invalid CameraParams buffer");
            }
        };

        static Result<wgpu::BindGroupLayout> CreateLayout(wgpu::Device device);

        static Result<wgpu::BindGroup> CreateBindGroup(wgpu::Device device, wgpu::BindGroupLayout layout,
            const Resources& resources);
    };

    static const char* GetShaderPath() { return ShaderPath; }

    static const char* GetEntryPoint() { return Entry; }
};