#pragma once

#include "GpuColorPass.h"
#include "GpuCompositorPass.h"
#include "GpuTransformPass.h"
#include "SceneTypes.h"
#include "Result.h"

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

    static Result<Renderer> Create(GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    Result<> Render(const GpuHelper& gpuHelper,
        const Camera& camera,
        const TrTransformf& cameraXForm,
        const Scene& scene,
        const PropKit& propKit);

    Result<> Composite(GpuHelper& gpuHelper, const ValidTexture& target);

    Result<> Composite(
        GpuHelper& gpuHelper, const ValidTexture& target, const Rect& dstRect);

private:

    Renderer(GpuColorPass&& colorPass,
        GpuCompositorPass&& compositorPass,
        GpuTransformPass&& transformPass)
        : m_ColorPass(std::move(colorPass))
        , m_CompositorPass(std::move(compositorPass))
        , m_TransformPass(std::move(transformPass))
    {
    }

    Result<> TransformNodes(const GpuHelper& gpuHelper,
        const wgpu::CommandEncoder& cmdEncoder,
        const TrTransformf& cameraXForm,
        const Camera& camera,
        const Scene& scene);

    std::optional<GpuColorPass::Outputs> m_ColorPassOutputs;
    GpuColorPass m_ColorPass;
    GpuCompositorPass m_CompositorPass;
    GpuTransformPass m_TransformPass;

    std::vector<MeshInstance> m_VisibleMeshes;
};