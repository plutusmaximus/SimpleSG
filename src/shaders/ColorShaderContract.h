#pragma once

#include "WebgpuHelper.h"

// Strongly-typed GPU storage buffer classes.
using WorldTransformBuffer = SemanticGpuBuffer<ShaderInterop::WorldTransform>;
using ClipSpaceBuffer = SemanticGpuBuffer<ShaderInterop::ClipSpaceTransform>;
using MeshPropertiesBuffer = SemanticGpuBuffer<ShaderInterop::MeshProperties>;
using CameraParamsBuffer = SemanticGpuBuffer<ShaderInterop::CameraParams>;
using MaterialConstantsBuffer = SemanticGpuBuffer<ShaderInterop::MaterialConstants>;

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
        };

        static wgpu::BindGroupLayout CreateLayout(wgpu::Device device);

        static wgpu::BindGroup CreateBindGroup(wgpu::Device device, wgpu::BindGroupLayout layout,
            const Resources& resources);
    };

    struct MaterialGroup
    {
        static constexpr uint32_t GroupIndex = 1;

        struct Resources
        {
            Texture BaseTexture;
            wgpu::Sampler BaseSampler;
        };

        static wgpu::BindGroupLayout CreateLayout(wgpu::Device device);

        static wgpu::BindGroup CreateBindGroup(wgpu::Device device, wgpu::BindGroupLayout layout,
            const Resources& resources);
    };

    static wgpu::VertexBufferLayout GetVertexBufferLayout();

    static const char* GetShaderPath() { return ShaderPath; }

    static const char* GetVertexEntryPoint() { return VertexEntry; }

    static const char* GetFragmentEntryPoint() { return FragmentEntry; }
};