#pragma once

#include "Camera.h"
#include "GpuTypes.h"

#include <optional>

class FileFetcher;
class GpuHelper;

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
        ValidTexture RenderTarget;
        ValidTexture DepthBuffer;

        friend bool operator==(const Outputs& a, const Outputs& b)
        {
            return a.RenderTarget == b.RenderTarget && a.DepthBuffer == b.DepthBuffer;
        }
    };

    GpuColorPass() = delete;
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

    explicit GpuColorPass(wgpu::ShaderModule shader)
        : m_Shader(std::move(shader))
    {
    }

    Result<> EnsurePipeline(const GpuHelper& gpuHelper);

    std::optional<Inputs> m_Inputs;
    std::optional<Outputs> m_Outputs;

    wgpu::ShaderModule m_Shader;
    wgpu::BindGroupLayout m_InputsBindGroupLayout;
    wgpu::BindGroupLayout m_TextureBindGroupLayout;
    wgpu::PipelineLayout m_PipelineLayout;
    wgpu::BindGroup m_InputsBindGroup;
    wgpu::RenderPipeline m_Pipeline;
};