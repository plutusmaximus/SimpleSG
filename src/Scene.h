#pragma once

#include "CoopTask.h"
#include "GpuColorPass.h"
#include "GpuCompositorPass.h"
#include "GpuTransformPass.h"
#include "GpuTypes.h"
#include "LevelTypes.h"
#include "TextureFetcher.h"
#include "Timer.h"

#include <filesystem>
#include <memory>
#include <vector>

class ResourceBundle;
class System;
class ThreadPool;

class Scene
{
public:

    Scene() = delete;
    ~Scene() = default;
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    Result<> Render(const Camera& camera, const TrTransformf& cameraXForm);

    Result<> Composite(const GpuRenderTarget& target);

    Result<> Composite(const GpuRenderTarget& target, const Rect& dstRect);

    class CreateTask;

private:
    Scene(const GpuHelper& gpuHelper,
        const Level& level,
        GpuColorPass&& colorPass,
        GpuCompositorPass&& compositorPass,
        GpuTransformPass&& transformPass,
        GpuVertexBuffer&& vertexBuffer,
        GpuIndexBuffer&& indexBuffer,
        GpuWorldTransformBuffer&& worldTransformBuffer,
        GpuClipSpaceBuffer&& clipSpaceBuffer,
        GpuMeshInstanceParamsBuffer&& meshInstanceParamsBuffer,
        GpuCameraParamsBuffer&& cameraParamsBuffer,
        std::vector<wgpu::BindGroup>&& materialBindGroups);

    void CollectVisibleMeshes(const Frustum& frustum,
        std::vector<MeshInstance>& outVisibleMeshes) const;

    // Sync updates from CPU -> GPU.
    Result<> SyncToGpu();

    Result<> TransformNodes(const wgpu::Device& gpuDevice,
        const wgpu::CommandEncoder& cmdEncoder,
        const TrTransformf& cameraXForm,
        const Camera& camera);

    const GpuHelper* m_GpuHelper{ nullptr };

    const Level* m_Level{ nullptr };

    std::optional<GpuColorPass::Outputs> m_ColorPassOutputs;
    GpuColorPass m_ColorPass;
    GpuCompositorPass m_CompositorPass;
    GpuTransformPass m_TransformPass;

    GpuVertexBuffer m_VertexBuffer;
    GpuIndexBuffer m_IndexBuffer;
    GpuWorldTransformBuffer m_WorldTransformBuffer;
    GpuClipSpaceBuffer m_ClipSpaceBuffer;
    GpuMeshInstanceParamsBuffer m_MeshInstanceParamsBuffer;
    GpuCameraParamsBuffer m_CameraParamsBuffer;

    std::vector<wgpu::BindGroup> m_MaterialBindGroups;

    std::vector<MeshInstance> m_VisibleMeshes;
};

class Scene::CreateTask : public ICoopTask<>
{
public:
    CreateTask(System& system,
        std::filesystem::path rootPath,
        const ResourceBundle& resourceBundle,
        const Level& level);

    CreateTask() = delete;
    ~CreateTask() override = default;
    CreateTask(const CreateTask&) = delete;
    CreateTask& operator=(const CreateTask&) = delete;
    CreateTask(CreateTask&&) = delete;
    CreateTask& operator=(CreateTask&&) = delete;

    Result<std::unique_ptr<Scene>> Take();

private:

    enum class Stage
    {
        None,
        Running,
        Succeeded,
        Failed
    };

    Result<> OnStart() override;

    void OnUpdate() override;

    Timer m_Timer;
    System* m_System{ nullptr };
    const ResourceBundle* m_ResourceBundle{ nullptr };
    const Level* m_Level{ nullptr };
    std::vector<std::string> m_TexturePaths;

    TextureFetcher m_TextureFetcher;
    GpuColorPass::CreateTask m_ColorPassTask;
    GpuCompositorPass::CreateTask m_CompositorPassTask;
    GpuTransformPass::CreateTask m_TransformPassTask;
    CoopTaskBatch m_TaskBatch;
    bool m_Consumed{ false };

    Stage m_Stage{ Stage::None };
};