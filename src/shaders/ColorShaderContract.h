#pragma once

#include "GpuBufferTypes.h"
#include "Result.h"

class ColorShaderContract
{
    static constexpr const char* ShaderPath = "shaders/ColorShader.wgsl";
    static constexpr const char* VertexEntry = "vs_main";
    static constexpr const char* FragmentEntry = "fs_main";

public:

    using VertexType = Vertex;

    struct SceneGroup
    {
        static constexpr uint32_t GroupIndex = 0;

        struct Resources
        {
            WorldTransformBuffer WorldTransforms;
            ClipSpaceBuffer ClipSpaceTransforms;
            MeshPropertiesBuffer MeshProperties;
            MaterialConstantsBuffer MaterialConstants;
            CameraParamsBuffer CameraParams;

            bool Validate() const
            {
                return MLG_VERIFY(WorldTransforms, "Invalid WorldTransforms buffer") &&
                       MLG_VERIFY(ClipSpaceTransforms, "Invalid ClipSpaceTransforms buffer") &&
                       MLG_VERIFY(MeshProperties, "Invalid MeshProperties buffer") &&
                       MLG_VERIFY(MaterialConstants, "Invalid MaterialConstants buffer") &&
                       MLG_VERIFY(CameraParams, "Invalid CameraParams buffer");
            }
        };

        static Result<wgpu::BindGroupLayout> CreateLayout(const wgpu::Device& gpuDevice);

        static Result<wgpu::BindGroup> CreateBindGroup(const wgpu::Device& gpuDevice, wgpu::BindGroupLayout layout,
            const Resources& resources);
    };

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

    static wgpu::VertexBufferLayout GetVertexBufferLayout();

    static const char* GetShaderPath() { return ShaderPath; }

    static const char* GetVertexEntryPoint() { return VertexEntry; }

    static const char* GetFragmentEntryPoint() { return FragmentEntry; }
};