#pragma once

#include "Result.h"
#include "shaders/GpuBufferTypes.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>
#include <webgpu/webgpu_cpp.h>

class FileFetcher;
class GpuHelper;

class GpuColorPass
{
public:
    static constexpr const char* ShaderPath = "shaders/ColorShader.wgsl";
    static constexpr const char* VertexEntry = "vs_main";
    static constexpr const char* FragmentEntry = "fs_main";
    static constexpr wgpu::TextureFormat kColorTargetFormat = wgpu::TextureFormat::RGBA8Unorm;
    static constexpr wgpu::TextureFormat kDepthTargetFormat = wgpu::TextureFormat::Depth24Plus;
    static constexpr float kClearDepth = 1.0f;

    static constexpr const char* CompositorShaderPath = "shaders/CompositorShader.wgsl";
    static constexpr const char* CompositorVertexEntry = "vs_main";
    static constexpr const char* CompositorFragmentEntry = "fs_main";

    struct TargetResources
    {
        wgpu::Texture Target;
        wgpu::Texture DepthTarget;

        Result<> Validate() const
        {
            MLG_CHECKV(Target, "Target texture is not valid");
            MLG_CHECKV(DepthTarget, "Depth target texture is not valid");
            MLG_CHECKV(Target.GetFormat() == kColorTargetFormat, "Invalid target texture format");
            MLG_CHECKV(DepthTarget.GetFormat() == kDepthTargetFormat,
                "Invalid depth target texture format");

            return Result<>::Ok;
        }

        friend bool operator==(const TargetResources& a, const TargetResources& b)
        {
            return a.Target.Get() == b.Target.Get() && a.DepthTarget.Get() == b.DepthTarget.Get();
        }
    };

    struct Resources
    {
        VertexBuffer Vertices;
        IndexBuffer Indices;
        WorldTransformBuffer WorldTransforms;
        ClipSpaceBuffer ClipSpaceTransforms;
        MeshPropertiesBuffer MeshProperties;
        MaterialConstantsBuffer MaterialConstants;
        CameraParamsBuffer CameraParams;

        Result<> Validate() const
        {
            MLG_CHECKV(Vertices, "Vertices buffer is not valid");
            MLG_CHECKV(Indices, "Indices buffer is not valid");
            MLG_CHECKV(WorldTransforms, "World transforms buffer is not valid");
            MLG_CHECKV(ClipSpaceTransforms, "Clip space transforms buffer is not valid");
            MLG_CHECKV(MeshProperties, "Mesh properties buffer is not valid");
            MLG_CHECKV(MaterialConstants, "Material constants buffer is not valid");
            MLG_CHECKV(CameraParams, "Camera params buffer is not valid");

            return Result<>::Ok;
        }

        friend bool operator==(const Resources& a, const Resources& b)
        {
            return a.Vertices.GetGpuBuffer().Get()
                == b.Vertices.GetGpuBuffer().Get()
                && a.Indices.GetGpuBuffer().Get()
                == b.Indices.GetGpuBuffer().Get()
                && a.WorldTransforms.GetGpuBuffer().Get()
                == b.WorldTransforms.GetGpuBuffer().Get()
                && a.ClipSpaceTransforms.GetGpuBuffer().Get()
                == b.ClipSpaceTransforms.GetGpuBuffer().Get()
                && a.MeshProperties.GetGpuBuffer().Get()
                == b.MeshProperties.GetGpuBuffer().Get()
                && a.MaterialConstants.GetGpuBuffer().Get()
                == b.MaterialConstants.GetGpuBuffer().Get()
                && a.CameraParams.GetGpuBuffer().Get()
                == b.CameraParams.GetGpuBuffer().Get();
        }
    };

    ~GpuColorPass() = default;
    GpuColorPass(const GpuColorPass&) = delete;
    GpuColorPass& operator=(const GpuColorPass&) = delete;
    GpuColorPass(GpuColorPass&&) = default;
    GpuColorPass& operator=(GpuColorPass&&) = default;

    static Result<GpuColorPass> Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    static Result<TargetResources> CreateTarget(
        const wgpu::Device& gpuDevice, const uint32_t width, const uint32_t height);

    Result<> BindResources(const GpuHelper& gpuHelper,
        const Resources& resources,
        const TargetResources& targetResources);

    Result<wgpu::RenderPassEncoder> BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder);

    Result<> Composite(const wgpu::Device& gpuDevice, const wgpu::Texture& target) const;

    Result<> Composite(
        const wgpu::Device& gpuDevice, const wgpu::Texture& target, const Rect& dstRect) const;

private:
    GpuColorPass() = default;

    struct PipelineResources
    {
        wgpu::ShaderModule Shader;
        std::array<wgpu::BindGroupLayout, 2> BindGroupLayouts;
        wgpu::PipelineLayout Layout;
    };

    struct CompositorPipelineResources
    {
        wgpu::ShaderModule Shader;
        wgpu::BindGroupLayout BindGroupLayout;
        wgpu::PipelineLayout Layout;
        wgpu::Sampler Sampler;
        wgpu::TextureFormat TargetFormat{ wgpu::TextureFormat::Undefined };
    };

    Result<> EnsurePipeline(const wgpu::Device& gpuDevice);

    Result<> EnsureCompositorPipeline(const wgpu::Device& gpuDevice,
        wgpu::TextureFormat targetFormat);

    std::optional<Resources> m_Resources;

    TargetResources m_TargetResources;
    PipelineResources m_PipelineResources;
    wgpu::BindGroup m_BindGroup;
    wgpu::RenderPipeline m_Pipeline;
    std::vector<uint8_t> m_ShaderCode;

    CompositorPipelineResources m_CompositorPipelineResources;
    wgpu::BindGroup m_CompositorBindGroup;
    wgpu::RenderPipeline m_CompositorPipeline;
    std::vector<uint8_t> m_CompositorShaderCode;
};