#pragma once

#include "GpuColorPass.h"
#include "GpuCompositorPass.h"
#include "GpuTransformPass.h"
#include "SceneTypes.h"
#include "Result.h"

#include <memory>

class FileFetcher;
class GpuHelper;

class Camera;
class PropKit;
class Scene;

class Renderer
{
public:

    Renderer() = delete;
    ~Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = default;
    Renderer& operator=(Renderer&&) = default;

    static Result<std::unique_ptr<class Renderer>> Create(GpuHelper& gpuHelper,
        FileFetcher& fileFetcher);

    Result<> Render(const Camera& camera,
        const TrTransformf& cameraXForm,
        const Scene& scene,
        const PropKit& propKit);

    Result<> Composite(const GpuValidTexture& target);

    Result<> Composite(const GpuValidTexture& target, const Rect& dstRect);

private:

    Renderer(const GpuHelper& gpuHelper,
        GpuColorPass&& colorPass,
        GpuCompositorPass&& compositorPass,
        GpuTransformPass&& transformPass)
        : m_GpuHelper(&gpuHelper)
        , m_ColorPass(std::move(colorPass))
        , m_CompositorPass(std::move(compositorPass))
        , m_TransformPass(std::move(transformPass))
    {
    }

    Result<> TransformNodes(const wgpu::Device& gpuDevice,
        const wgpu::CommandEncoder& cmdEncoder,
        const TrTransformf& cameraXForm,
        const Camera& camera,
        const Scene& scene);

    const GpuHelper* m_GpuHelper{ nullptr };

    std::optional<GpuColorPass::Outputs> m_ColorPassOutputs;
    GpuColorPass m_ColorPass;
    GpuCompositorPass m_CompositorPass;
    GpuTransformPass m_TransformPass;

    std::vector<MeshInstance> m_VisibleMeshes;
};