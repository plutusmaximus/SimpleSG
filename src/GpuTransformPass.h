#pragma once

#include "CoopTask.h"
#include "GpuTypes.h"
#include "Result.h"
#include "ShaderFetcher.h"

#include <memory>
#include <optional>

class FileFetcher;
class GpuHelper;

class GpuTransformPass
{
public:
    class CreateTask;

    struct Inputs
    {
        GpuWorldTransformBuffer WorldTransforms;
        GpuCameraParamsBuffer CameraParams;

        Result<> Validate() const // NOLINT(readability-convert-member-functions-to-static)
        {
            return Result<>::Ok;
        }

        friend bool operator==(const Inputs& a, const Inputs& b) = default;
    };

    struct Outputs
    {
        GpuClipSpaceBuffer ClipSpaceTransforms;

        Result<> Validate() const // NOLINT(readability-convert-member-functions-to-static)
        {
            return Result<>::Ok;
        }

        friend bool operator==(const Outputs& a, const Outputs& b) = default;
    };

    GpuTransformPass() = delete;
    ~GpuTransformPass() = default;
    GpuTransformPass(const GpuTransformPass&) = delete;
    GpuTransformPass& operator=(const GpuTransformPass&) = delete;
    GpuTransformPass(GpuTransformPass&&) = delete;
    GpuTransformPass& operator=(GpuTransformPass&&) = delete;

    Result<> SetInputs(const Inputs& inputs);
    Result<> SetOutputs(const Outputs& outputs);

    /// Executes the pass using the supplied inputs and outputs.
    /// This variant of Execute creates a command encoder that's owned and
    /// submitted to the GPU before returning.
    Result<> Execute();

    /// Executes the pass using the supplied inputs and outputs.
    /// This variant of Execute uses the provided command encoder.
    /// The caller is responsible for submitting the command encoder to the GPU.
    Result<> Execute(wgpu::CommandEncoder cmdEncoder);

private:
    static constexpr const char* ShaderPath = "shaders/TransformShader.wgsl";
    static constexpr const char* ComputeEntry = "cs_main";
    static constexpr const char* kWorkgroupSizeOverride = "WorkgroupSizeOverride";
    static constexpr size_t kWorkgroupSize = 64;

    explicit GpuTransformPass(const GpuHelper& gpuHelper,
        wgpu::ShaderModule shader,
        wgpu::BindGroupLayout bindGroupLayout,
        wgpu::PipelineLayout pipelineLayout)
        : m_GpuHelper(&gpuHelper),
          m_Shader(std::move(shader)),
          m_BindGroupLayout(std::move(bindGroupLayout)),
          m_PipelineLayout(std::move(pipelineLayout))
    {
        MLG_ASSERT(m_Shader, "Shader module is not valid");
        MLG_ASSERT(m_BindGroupLayout, "Bind group layout is not valid");
        MLG_ASSERT(m_PipelineLayout, "Pipeline layout is not valid");
    }

    Result<> EnsurePipeline();
    Result<> EnsureInputOutputBindGroup();
    Result<wgpu::ComputePassEncoder> CreateComputePassEncoder(const wgpu::CommandEncoder& cmdEncoder);

    const GpuHelper* m_GpuHelper;

    std::optional<Inputs> m_Inputs;
    std::optional<Outputs> m_Outputs;

    wgpu::ShaderModule m_Shader;
    wgpu::BindGroupLayout m_BindGroupLayout;
    wgpu::PipelineLayout m_PipelineLayout;
    wgpu::BindGroup m_InputOutputBindGroup;
    wgpu::ComputePipeline m_Pipeline;
};

class GpuTransformPass::CreateTask : public ICoopTask<>
{
public:
    CreateTask(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);
    ~CreateTask() override = default;
    CreateTask(const CreateTask&) = delete;
    CreateTask& operator=(const CreateTask&) = delete;
    CreateTask(CreateTask&&) = delete;
    CreateTask& operator=(CreateTask&&) = delete;

    Result<std::unique_ptr<GpuTransformPass>> Take();

private:
    enum class Stage
    {
        None,
        FetchingShader,
        Succeeded,
        Failed
    };

    Result<> OnStart() override;

    void OnUpdate() override;

    const GpuHelper* m_GpuHelper{ nullptr };
    ShaderFetcher m_ShaderFetcher;

    Stage m_Stage{ Stage::None };

    bool m_Consumed{ false };
};