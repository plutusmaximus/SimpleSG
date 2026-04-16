#pragma once

#include "Result.h"

#include <unordered_map>
#include <webgpu/webgpu_cpp.h>

struct SDL_Window;
template<typename T>
class Mat44;
using Mat44f = Mat44<float>;
class DawnRenderCompositor;
class SceneKit;

class DawnRenderer
{
public:

    DawnRenderer() = delete;
    DawnRenderer(const DawnRenderer&) = delete;
    DawnRenderer& operator=(const DawnRenderer&) = delete;
    DawnRenderer(DawnRenderer&&) = delete;
    DawnRenderer& operator=(DawnRenderer&&) = delete;

    static Result<DawnRenderer*> Create(SDL_Window* window, wgpu::Device device, wgpu::Surface surface);

    static Result<> Destroy(DawnRenderer* renderer);

    Result<> Render(const Mat44f& camera,
        const Mat44f& projection,
        const SceneKit& sceneKit,
        DawnRenderCompositor* compositor);

private:

    DawnRenderer(SDL_Window* window, wgpu::Device device, wgpu::Surface surface);

    ~DawnRenderer();

    Result<wgpu::RenderPassEncoder> BeginRenderPass(wgpu::CommandEncoder cmdEncoder);

    /// @brief Resolve the color target to the swapchain texture.
    Result<> ResolveColorTargetToSwapchain(DawnRenderCompositor* compositor);

    Result<> CreateColorAndDepthTargets();

    Result<> CreateColorPipeline();

    Result<> CreateResolvePipeline();

    Result<> CreateTransformPipeline();

    Result<wgpu::ShaderModule> CreateShader(const char* path);

    Result<> TransformNodes(wgpu::CommandEncoder cmdEncoder,
        const Mat44f& camera,
        const Mat44f& projection,
        const SceneKit& sceneKit);

    SDL_Window* const m_Window;
    wgpu::Device m_WgpuDevice;
    wgpu::Surface m_Surface;
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

    struct TransformBuffers
    {
        size_t TransformCount{0};
        wgpu::Buffer ClipSpaceBuf;
        wgpu::Buffer ViewProjBuf;
        wgpu::BindGroup BindGroup1;
        wgpu::BindGroup BindGroup2;
    };

    TransformBuffers m_TransformBuffers;

    wgpu::ShaderModule m_TransformShader;
    wgpu::ComputePipeline m_TransformPipeline;
};