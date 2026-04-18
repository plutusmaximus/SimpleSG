#pragma once

#include "Result.h"
#include "shaders/ShaderTypes.h"

#include "WebgpuHelper.h"

#include <unordered_map>

class DawnRenderCompositor;
template<typename T>
class Mat44;
using Mat44f = Mat44<float>;
class Projection;
class SceneKit;

class DawnRenderer
{
public:

    DawnRenderer() = default;
    DawnRenderer(const DawnRenderer&) = delete;
    DawnRenderer& operator=(const DawnRenderer&) = delete;
    DawnRenderer(DawnRenderer&&) = delete;
    DawnRenderer& operator=(DawnRenderer&&) = delete;

    ~DawnRenderer()
    {
        Shutdown();
    }

    Result<> Startup();

    Result<> Shutdown();

    Result<> Render(const Mat44f& camera,
        const Projection& projection,
        const SceneKit& sceneKit,
        DawnRenderCompositor& compositor);

private:

    Result<wgpu::RenderPassEncoder> BeginRenderPass(wgpu::CommandEncoder cmdEncoder);

    /// @brief Resolve the color target to the swapchain texture.
    Result<> ResolveColorTargetToSwapchain(DawnRenderCompositor& compositor);

    Result<> CreateColorAndDepthTargets();

    Result<> CreateColorPipeline();

    Result<> CreateResolvePipeline();

    Result<> CreateTransformPipeline();

    Result<wgpu::ShaderModule> CreateShader(const char* path);

    Result<> TransformNodes(wgpu::CommandEncoder cmdEncoder,
        const Mat44f& camera,
        const Projection& projection,
        const SceneKit& sceneKit);

    wgpu::Limits m_GpuLimits;
    wgpu::Texture m_ColorTarget;
    wgpu::TextureView m_ColorTargetView;
    wgpu::Sampler m_ColorTargetSampler;
    wgpu::Texture m_DepthTarget;
    wgpu::TextureView m_DepthTargetView;

    struct ColorPipeline
    {
        wgpu::ShaderModule Shader;
        wgpu::PipelineLayout Layout;
        wgpu::BindGroup BindGroup0;
        wgpu::BindGroup BindGroup1;
        wgpu::BindGroup BindGroup2;
        wgpu::RenderPipeline Pipeline;
    };

    struct ResolvePipeline
    {
        wgpu::ShaderModule Shader;
        wgpu::PipelineLayout Layout;
        wgpu::BindGroup BindGroup2;
        wgpu::RenderPipeline Pipeline;
    };

    // Pipeline for rendering to the color target texture.
    ColorPipeline m_ColorPipeline;

    // Pipeline to resolve the color target to the swap chain.
    ResolvePipeline m_ResolvePipeline;

    using ClipSpaceBuffer = TypedGpuBuffer<ShaderTypes::ClipSpaceTransform>;
    using CameraParamsBuffer = TypedGpuBuffer<ShaderTypes::CameraParams>;

    struct TransformBuffers
    {
        size_t TransformCount{0};
        ClipSpaceBuffer ClipSpaceBuf;
        CameraParamsBuffer CameraParamsBuf;
        wgpu::BindGroup BindGroup1;
        wgpu::BindGroup BindGroup2;
    };

    TransformBuffers m_TransformBuffers;

    wgpu::ShaderModule m_TransformShader;
    wgpu::ComputePipeline m_TransformPipeline;

    bool m_Initialized{false};
};