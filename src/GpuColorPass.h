#pragma once

#include "GpuHelper.h"

#include <optional>
#include <webgpu/webgpu_cpp.h>

class FileFetcher;

class GpuColorPass
{
public:
    static constexpr const char* ShaderPath = "shaders/ColorShader.wgsl";
    static constexpr const char* VertexEntry = "vs_main";
    static constexpr const char* FragmentEntry = "fs_main";
    static constexpr float kClearDepth = 1.0f;

    struct Inputs
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

        friend bool operator==(const Inputs& a, const Inputs& b)
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

    struct Outputs
    {
        wgpu::Texture RenderTarget;
        wgpu::Texture DepthBuffer;

        Result<> Validate() const
        {
            MLG_CHECKV(RenderTarget, "Render target texture is not valid");
            MLG_CHECKV(DepthBuffer, "Depth buffer texture is not valid");
            MLG_CHECKV(RenderTarget.GetFormat() == GpuHelper::kTextureFormat, "Invalid render target texture format");
            MLG_CHECKV(DepthBuffer.GetFormat() == GpuHelper::kDepthBufferFormat,
                "Invalid depth buffer format");

            return Result<>::Ok;
        }

        friend bool operator==(const Outputs& a, const Outputs& b)
        {
            return a.RenderTarget.Get()
                == b.RenderTarget.Get()
                && a.DepthBuffer.Get()
                == b.DepthBuffer.Get();
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

    ~GpuColorPass() = default;
    GpuColorPass(const GpuColorPass&) = delete;
    GpuColorPass& operator=(const GpuColorPass&) = delete;
    GpuColorPass(GpuColorPass&&) = default;
    GpuColorPass& operator=(GpuColorPass&&) = default;

    static Result<GpuColorPass> Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<wgpu::BindGroup> CreateTextureBindGroup(const GpuHelper& gpuHelper,
        const TextureResources& resources);

    Result<> SetInputs(const GpuHelper& gpuHelper, const Inputs& inputs);
    Result<> SetOutputs(const GpuHelper& gpuHelper, const Outputs& outputs);

    Result<wgpu::RenderPassEncoder> BeginPass(const wgpu::CommandEncoder& cmdEncoder) const;

private:
    GpuColorPass() = default;

    Result<> EnsurePipeline(const wgpu::Device& gpuDevice);

    std::optional<Inputs> m_Inputs;
    Outputs m_Outputs;

    wgpu::ShaderModule m_Shader;
    wgpu::BindGroupLayout m_InputsBindGroupLayout;
    wgpu::BindGroupLayout m_TextureBindGroupLayout;
    wgpu::PipelineLayout m_PipelineLayout;
    wgpu::BindGroup m_InputsBindGroup;
    wgpu::RenderPipeline m_Pipeline;
};