#pragma once

#include "GpuColorPass.h"
#include "GpuCompositorPass.h"
#include "GpuTransformPass.h"
#include "SceneTypes.h"
#include "Result.h"

#include <webgpu/webgpu_cpp.h>

class FileFetcher;
class GpuHelper;

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

    Result<> TransformNodes(const GpuHelper& gpuHelper,
        const wgpu::CommandEncoder& cmdEncoder,
        const TrTransformf& cameraXForm,
        const Camera& camera,
        const Scene& scene);

    wgpu::Limits m_GpuLimits;

    GpuColorPass::Outputs m_ColorPassOutputs;
    std::optional<GpuColorPass> m_ColorPass;
    std::optional<GpuCompositorPass> m_CompositorPass;
    std::optional<GpuTransformPass> m_TransformPass;

    std::vector<MeshInstance> m_VisibleMeshes;

    bool m_Initialized{false};
};