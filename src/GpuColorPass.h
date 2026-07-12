#pragma once

#include "Camera.h"
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
        Viewport Viewport;
        VertexBuffer Vertices;
        IndexBuffer Indices;
        WorldTransformBuffer WorldTransforms;
        ClipSpaceBuffer ClipSpaceTransforms;
        MeshPropertiesBuffer MeshProperties;
        MaterialConstantsBuffer MaterialConstants;
        CameraParamsBuffer CameraParams;

        friend bool operator==(const Inputs& a, const Inputs& b)
        {
            return a.Viewport == b.Viewport
                && a.Vertices == b.Vertices
                && a.Indices == b.Indices
                && a.WorldTransforms == b.WorldTransforms
                && a.ClipSpaceTransforms == b.ClipSpaceTransforms
                && a.MeshProperties == b.MeshProperties
                && a.MaterialConstants == b.MaterialConstants
                && a.CameraParams == b.CameraParams;
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
            MLG_CHECKV(RenderTarget.GetFormat() == GpuHelper::kTextureFormat,
                "Invalid render target texture format");
            MLG_CHECKV(DepthBuffer.GetFormat() == GpuHelper::kDepthBufferFormat,
                "Invalid depth buffer format");

            return Result<>::Ok;
        }

        friend bool operator==(const Outputs& a, const Outputs& b)
        {
            return a.RenderTarget.Get() == b.RenderTarget.Get()
                && a.DepthBuffer.Get() == b.DepthBuffer.Get();
        }
    };

    ~GpuColorPass() = default;
    GpuColorPass(const GpuColorPass&) = delete;
    GpuColorPass& operator=(const GpuColorPass&) = delete;
    GpuColorPass(GpuColorPass&&) = default;
    GpuColorPass& operator=(GpuColorPass&&) = default;

    static Result<GpuColorPass> Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> SetInputs(const GpuHelper& gpuHelper, const Inputs& inputs);
    Result<> SetOutputs(const GpuHelper& gpuHelper, const Outputs& outputs);

    Result<wgpu::RenderPassEncoder> BeginPass(const wgpu::CommandEncoder& cmdEncoder) const;

private:
    GpuColorPass() = default;

    Result<> EnsurePipeline(const GpuHelper& gpuHelper);

    std::optional<Inputs> m_Inputs;
    Outputs m_Outputs;

    wgpu::ShaderModule m_Shader;
    wgpu::BindGroupLayout m_InputsBindGroupLayout;
    wgpu::BindGroupLayout m_TextureBindGroupLayout;
    wgpu::PipelineLayout m_PipelineLayout;
    wgpu::BindGroup m_InputsBindGroup;
    wgpu::RenderPipeline m_Pipeline;
};