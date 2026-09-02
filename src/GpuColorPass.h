#pragma once

#include "Camera.h"
#include "GpuTypes.h"

#include <optional>

class FileFetcher;
class GpuHelper;
class MeshInstance;
class PropKit;

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
        GpuMeshInstanceParamsBuffer MeshInstanceParams;
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
                && a.MeshInstanceParams == b.MeshInstanceParams
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

    class Invocation
    {
    public:
        Invocation() = delete;
        ~Invocation();
        Invocation(const Invocation&) = delete;
        Invocation& operator=(const Invocation&) = delete;
        Invocation(Invocation&&) = default;
        Invocation& operator=(Invocation&&) = delete;

        Result<> Execute(const std::span<MeshInstance> visibleMeshes, const PropKit& propKit);

    private:
        friend class GpuColorPass;

        Invocation(wgpu::Device gpuDevice, wgpu::RenderPassEncoder renderPass)
            : m_GpuDevice(std::move(gpuDevice)),
              m_RenderPass(std::move(renderPass))
        {
        }

        wgpu::Device m_GpuDevice;
        wgpu::RenderPassEncoder m_RenderPass;
        wgpu::CommandEncoder m_CmdEncoder;
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

    /// @brief Prepares an invocation of the pass for execution.
    /// This variant of Prepare creates a command encoder that's owned and
    /// submitted to the GPU by the invocation.
    Result<Invocation> Prepare();

    /// @brief Prepares an invocation of the pass for execution.
    /// This variant of Prepare uses the provided command encoder.
    /// The caller is responsible for submitting the command encoder to the GPU.
    Result<Invocation> Prepare(const wgpu::CommandEncoder& cmdEncoder);

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