#pragma once

#include "GpuColorPass.h"
#include "GpuCompositorPass.h"
#include "SceneTypes.h"
#include "Result.h"

#include <webgpu/webgpu_cpp.h>

class FileFetcher;
class GpuHelper;

template<typename T>
class Mat44;
using Mat44f = Mat44<float>;
class Camera;
class PropKit;
class Scene;

class Renderer
{
public:

    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    ~Renderer()
    {
        MLG_VERIFY(Shutdown());
    }

    Result<> Startup(GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> Shutdown();

    Result<> Render(const GpuHelper& gpuHelper,
        const Camera& camera,
        const TrTransformf& cameraXForm,
        const Scene& scene,
        const PropKit& propKit);

    Result<> Composite(GpuHelper& gpuHelper, const wgpu::Texture& target);

    Result<> Composite(
        GpuHelper& gpuHelper, const wgpu::Texture& target, const Rect& dstRect);

    Result<wgpu::Texture> GetTarget() const;//DO NOT SUBMIT

private:

    Result<> CreateTransformPipeline(const wgpu::Device& gpuDevice, FileFetcher& fileFetcher);

    Result<> TransformNodes(const wgpu::Device& gpuDevice,
        const wgpu::CommandEncoder& cmdEncoder,
        const TrTransformf& cameraXForm,
        const Camera& camera,
        const Scene& scene) const;

    wgpu::Limits m_GpuLimits;

    GpuColorPass::TargetResources m_TargetResources;
    std::optional<GpuColorPass> m_ColorPass;
    std::optional<GpuCompositorPass> m_CompositorPass;

    struct TransformPipelineResources
    {
        wgpu::ShaderModule Shader;
        wgpu::PipelineLayout Layout;
    };

    // Pipeline for computing world transforms on the GPU.
    TransformPipelineResources m_TransformPipelineResources;
    wgpu::ComputePipeline m_TransformPipeline;

    std::vector<MeshInstance> m_VisibleMeshes;

    bool m_Initialized{false};
};