#pragma once

#include "Result.h"
#include "shaders/GpuBufferTypes.h"

#include <array>
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

    struct TextureResources
    {
        wgpu::Texture Texture;
        wgpu::Sampler Sampler;

        Result<> Validate() const
        {
            MLG_CHECKV(Texture, "Texture is not valid");
            MLG_CHECKV(Sampler, "Sampler is not valid");

            return Result<>::Ok;
        }

        friend bool operator==(const TextureResources& a, const TextureResources& b)
        {
            return a.Texture.Get() == b.Texture.Get() && a.Sampler.Get() == b.Sampler.Get();
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
        const GpuHelper& gpuHelper, const uint32_t width, const uint32_t height);

    Result<wgpu::BindGroup> CreateTextureBindGroup(const GpuHelper& gpuHelper,
        const TextureResources& resources);

    Result<> BindResources(const GpuHelper& gpuHelper,
        const Resources& resources,
        const TargetResources& targetResources);

    Result<wgpu::RenderPassEncoder> BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder) const;

private:
    GpuColorPass() = default;

    struct PipelineResources
    {
        wgpu::ShaderModule Shader;
        std::array<wgpu::BindGroupLayout, 2> BindGroupLayouts;
        wgpu::PipelineLayout PipelineLayout;
    };

    Result<> EnsurePipeline(const wgpu::Device& gpuDevice);

    std::optional<Resources> m_Resources;

    TargetResources m_TargetResources;
    PipelineResources m_PipelineResources;
    wgpu::BindGroup m_BindGroup;
    wgpu::RenderPipeline m_Pipeline;
    std::vector<uint8_t> m_ShaderCode;
};