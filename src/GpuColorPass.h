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
        GpuVertexBuffer Vertices;
        GpuIndexBuffer Indices;
        GpuWorldTransformBuffer WorldTransforms;
        GpuClipSpaceBuffer ClipSpaceTransforms;
        GpuMeshPropertiesBuffer MeshProperties;
        GpuMaterialConstantsBuffer MaterialConstants;
        GpuCameraParamsBuffer CameraParams;

        Result<> Validate() const // NOLINT(readability-convert-member-functions-to-static)
        {
            return Result<>::Ok;
        }

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
        GpuRenderTarget RenderTarget;
        GpuDepthTarget DepthBuffer;

        Result<> Validate() const // NOLINT(readability-convert-member-functions-to-static)
        {
            return Result<>::Ok;
        }

        friend bool operator==(const Outputs& a, const Outputs& b) = default;
    };

    GpuColorPass() = delete;
    ~GpuColorPass() = default;
    GpuColorPass(const GpuColorPass&) = delete;
    GpuColorPass& operator=(const GpuColorPass&) = delete;
    GpuColorPass(GpuColorPass&&) = default;
    GpuColorPass& operator=(GpuColorPass&&) = default;

    static Result<GpuColorPass> Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> SetInputs(const Inputs& inputs);
    Result<> SetOutputs(const Outputs& outputs);

    Result<wgpu::RenderPassEncoder> BeginPass(const wgpu::CommandEncoder& cmdEncoder);

private:
    explicit GpuColorPass(const GpuHelper& gpuHelper,
        wgpu::ShaderModule shader,
        wgpu::BindGroupLayout inputsBindGroupLayout,
        wgpu::PipelineLayout pipelineLayout)
        : m_GpuHelper(&gpuHelper),
          m_Shader(std::move(shader)),
          m_InputsBindGroupLayout(std::move(inputsBindGroupLayout)),
          m_PipelineLayout(std::move(pipelineLayout))
    {
        MLG_ASSERT(m_Shader, "Shader module is not valid");
        MLG_ASSERT(m_InputsBindGroupLayout, "Inputs bind group layout is not valid");
        MLG_ASSERT(m_PipelineLayout, "Pipeline layout is not valid");
    }

    Result<> EnsurePipeline();
    Result<> EnsureInputsBindGroup();

    const GpuHelper* m_GpuHelper{ nullptr };

    std::optional<Inputs> m_Inputs;
    std::optional<Outputs> m_Outputs;

    wgpu::ShaderModule m_Shader;
    wgpu::BindGroupLayout m_InputsBindGroupLayout;
    wgpu::PipelineLayout m_PipelineLayout;
    wgpu::BindGroup m_InputsBindGroup;
    wgpu::RenderPipeline m_Pipeline;
};