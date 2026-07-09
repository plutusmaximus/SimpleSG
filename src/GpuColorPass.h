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
    static constexpr wgpu::TextureFormat kDepthTargetFormat = wgpu::TextureFormat::Depth24Plus;
    static constexpr float kClearDepth = 1.0f;

    static constexpr const char* CompositorShaderPath = "shaders/CompositorShader.wgsl";
    static constexpr const char* CompositorVertexEntry = "vs_main";
    static constexpr const char* CompositorFragmentEntry = "fs_main";

    struct Resources
    {
        VertexBuffer Vertices;
        IndexBuffer Indices;
        WorldTransformBuffer WorldTransforms;
        ClipSpaceBuffer ClipSpaceTransforms;
        MeshPropertiesBuffer MeshProperties;
        MaterialConstantsBuffer MaterialConstants;
        CameraParamsBuffer CameraParams;

        friend bool operator==(const Resources& lhs, const Resources& rhs)
        {
            return lhs.Vertices.GetGpuBuffer().Get()
                == rhs.Vertices.GetGpuBuffer().Get()
                && lhs.Indices.GetGpuBuffer().Get()
                == rhs.Indices.GetGpuBuffer().Get()
                && lhs.WorldTransforms.GetGpuBuffer().Get()
                == rhs.WorldTransforms.GetGpuBuffer().Get()
                && lhs.ClipSpaceTransforms.GetGpuBuffer().Get()
                == rhs.ClipSpaceTransforms.GetGpuBuffer().Get()
                && lhs.MeshProperties.GetGpuBuffer().Get()
                == rhs.MeshProperties.GetGpuBuffer().Get()
                && lhs.MaterialConstants.GetGpuBuffer().Get()
                == rhs.MaterialConstants.GetGpuBuffer().Get()
                && lhs.CameraParams.GetGpuBuffer().Get()
                == rhs.CameraParams.GetGpuBuffer().Get();
        }
    };

    ~GpuColorPass() = default;
    GpuColorPass(const GpuColorPass&) = delete;
    GpuColorPass& operator=(const GpuColorPass&) = delete;
    GpuColorPass(GpuColorPass&&) = default;
    GpuColorPass& operator=(GpuColorPass&&) = default;

    static Result<GpuColorPass> Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> Ensure(const GpuHelper& gpuHelper, const uint32_t width, const uint32_t height);

    Result<wgpu::Texture> GetTarget() const;

    Result<> BindResources(const wgpu::Device& device, const Resources& resources);

    Result<wgpu::RenderPassEncoder> BeginRenderPass(const wgpu::CommandEncoder& cmdEncoder);

    Result<wgpu::RenderPassEncoder> BeginCompositorRenderPass(
        const wgpu::CommandEncoder& cmdEncoder, wgpu::Texture target);

private:
    GpuColorPass() = default;

    struct TargetResources
    {
        wgpu::Texture Target;
        wgpu::TextureView TargetView;
        wgpu::Texture DepthTarget;
        wgpu::TextureView DepthTargetView;
    };

    struct PipelineResources
    {
        wgpu::ShaderModule Shader;
        std::array<wgpu::BindGroupLayout, 2> BindGroupLayouts;
        wgpu::PipelineLayout Layout;
        wgpu::TextureFormat TargetFormat{ wgpu::TextureFormat::Undefined };
        wgpu::TextureFormat DepthFormat{ wgpu::TextureFormat::Undefined };
    };

    struct CompositorPipelineResources
    {
        wgpu::ShaderModule Shader;
        wgpu::BindGroupLayout BindGroupLayout;
        wgpu::PipelineLayout Layout;
        wgpu::Sampler Sampler;
        wgpu::TextureFormat TargetFormat{ wgpu::TextureFormat::Undefined };
    };

    Result<> EnsureTarget(const wgpu::Device& gpuDevice,
        const uint32_t width,
        const uint32_t height,
        wgpu::TextureFormat targetFormat);

    Result<> EnsurePipeline(const wgpu::Device& gpuDevice);

    Result<> EnsureCompositorPipeline(const wgpu::Device& gpuDevice, wgpu::TextureFormat targetFormat);

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

class GpuCompositorPass
{
public:
    static constexpr const char* ShaderPath = "shaders/CompositorShader.wgsl";
    static constexpr const char* VertexEntry = "vs_main";
    static constexpr const char* FragmentEntry = "fs_main";

    struct Resources
    {
        wgpu::Texture SourceTexture;

        friend bool operator==(const Resources& lhs, const Resources& rhs)
        {
            return lhs.SourceTexture.Get() == rhs.SourceTexture.Get();
        }
    };

    ~GpuCompositorPass() = default;
    GpuCompositorPass(const GpuCompositorPass&) = delete;
    GpuCompositorPass& operator=(const GpuCompositorPass&) = delete;
    GpuCompositorPass(GpuCompositorPass&&) = default;
    GpuCompositorPass& operator=(GpuCompositorPass&&) = default;

    static Result<GpuCompositorPass> Create(const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> Ensure(const GpuHelper& gpuHelper);

    Result<> BindResources(const wgpu::Device& device, const Resources& resources);

    Result<wgpu::RenderPassEncoder> BeginRenderPass(
        const wgpu::CommandEncoder& cmdEncoder, wgpu::Texture target);

private:
    GpuCompositorPass() = default;

    struct PipelineResources
    {
        wgpu::ShaderModule Shader;
        wgpu::BindGroupLayout BindGroupLayout;
        wgpu::PipelineLayout Layout;
        wgpu::Sampler Sampler;
        wgpu::TextureFormat TargetFormat{ wgpu::TextureFormat::Undefined };
    };

    Result<> EnsurePipeline(const GpuHelper& gpuHelper);

    Resources m_Resources;
    PipelineResources m_PipelineResources;
    wgpu::BindGroup m_BindGroup;
    wgpu::RenderPipeline m_Pipeline;
    std::vector<uint8_t> m_ShaderCode;
};