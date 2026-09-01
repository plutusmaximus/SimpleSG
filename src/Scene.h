#pragma once

#include "GpuColorPass.h"
#include "GpuCompositorPass.h"
#include "GpuTransformPass.h"
#include "GpuTypes.h"
#include "Level.h"
#include "SceneTypes.h"

#include <vector>

class Scene
{
public:
    static Result<Scene> Create(const GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        const std::span<const ModelNode> modelNodes);

    Scene() = delete;
    ~Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&& other) = default;
    Scene& operator=(Scene&& other) = default;

    Result<> Render(const Camera& camera, const TrTransformf& cameraXForm, const PropKit& propKit);

    Result<> Composite(const GpuRenderTarget& target);

    Result<> Composite(const GpuRenderTarget& target, const Rect& dstRect);

private:
    Scene(const GpuHelper& gpuHelper,
        const std::span<const ModelNode> modelNodes,
        GpuColorPass&& colorPass,
        GpuCompositorPass&& compositorPass,
        GpuTransformPass&& transformPass,
        GpuWorldTransformBuffer&& worldTransformBuffer,
        GpuClipSpaceBuffer&& clipSpaceBuffer,
        GpuMeshInstanceParamsBuffer&& meshInstanceParamsBuffer,
        GpuCameraParamsBuffer&& cameraParamsBuffer);

    void CollectVisibleMeshes(const Frustum& frustum,
        std::vector<MeshInstance>& outVisibleMeshes) const;

    // Sync updates from CPU -> GPU.
    Result<> SyncToGpu();

    Result<> TransformNodes(const wgpu::Device& gpuDevice,
        const wgpu::CommandEncoder& cmdEncoder,
        const TrTransformf& cameraXForm,
        const Camera& camera);

    const GpuHelper* m_GpuHelper{ nullptr };

    std::span<const ModelNode> m_ModelNodes;

    std::optional<GpuColorPass::Outputs> m_ColorPassOutputs;
    GpuColorPass m_ColorPass;
    GpuCompositorPass m_CompositorPass;
    GpuTransformPass m_TransformPass;

    GpuWorldTransformBuffer m_WorldTransformBuffer;
    GpuClipSpaceBuffer m_ClipSpaceBuffer;
    GpuMeshInstanceParamsBuffer m_MeshInstanceParamsBuffer;
    GpuCameraParamsBuffer m_CameraParamsBuffer;
    
    std::vector<MeshInstance> m_VisibleMeshes;
};