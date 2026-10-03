#define MLG_LOGGER_NAME "SCEN"

#include "Scene.h"

#include "Camera.h"
#include "FileFetcher.h"
#include "GpuHelper.h"
#include "Level.h"
#include "PerfMetrics.h"
#include "ResourceBundle.h"
#include "System.h"
#include "TextureFetcher.h"

namespace
{

size_t
CountMeshInstances(const std::span<const ModelNode> modelNodes)
{
    size_t count = 0;

    for(const ModelNode& node : modelNodes)
    {
        count += node.GetMeshCount();
    }

    return count;
}

Result<GpuMeshInstanceParamsBuffer>
BuildMeshInstanceParamsBuffer(const GpuHelper& gpuHelper,
    const std::span<const ModelNode> modelNodes)
{
    const size_t meshInstanceCount = CountMeshInstances(modelNodes);
    std::vector<ShaderInterop::MeshInstanceParams> meshInstanceParams;
    meshInstanceParams.reserve(meshInstanceCount);

    uint32_t transformIndex = 0;

    for(const ModelNode& modelNode : modelNodes)
    {
        for(uint32_t i = 0; i < modelNode.GetMeshCount(); ++i)
        {
            const ShaderInterop::MeshInstanceParams mip //
                {
                    .TransformIndex = transformIndex,
                };

            meshInstanceParams.push_back(mip);
        }

        ++transformIndex;
    }

    auto buffer =
        gpuHelper.CreateStorageBuffer<GpuMeshInstanceParamsBuffer>(meshInstanceParams.size(),
            "MeshInstanceParamsBuffer");
    MLG_CHECK(buffer);

    buffer->Store(meshInstanceParams);

    return buffer;
}

Result<GpuColorPass::Outputs>
CreateColorPassTarget(const GpuHelper& gpuHelper, const uint32_t width, const uint32_t height)
{
    MLG_DEBUG("Creating new color/depth target with size {}x{}", width, height);

    auto renderTarget = gpuHelper.CreateRenderTarget(width, height, "ColorPass::RenderTarget");
    MLG_CHECK(renderTarget, "Failed to create color render target");

    auto depthBuffer = gpuHelper.CreateDepthBuffer(width, height, "ColorPass::DepthBuffer");
    MLG_CHECK(depthBuffer, "Failed to create color depth buffer");

    return GpuColorPass::Outputs //
        {
            .RenderTarget = *renderTarget,
            .DepthBuffer = *depthBuffer,
        };
}

Result<std::vector<wgpu::BindGroup>>
CreateMaterialBindGroups(const GpuHelper& gpuHelper,
    const GpuColorPass& gpuColorPass,
    const ResourceBundle& resourceBundle,
    const std::span<const wgpu::Texture> textures)
{
    const std::span materialRsrcs = resourceBundle.GetMaterials();
    const std::span textureRsrcs = resourceBundle.GetTextures();

    MLG_CHECKV(textures.size() == textureRsrcs.size(), "Texture count mismatch");

    std::vector<wgpu::BindGroup> materialBindGroups;
    materialBindGroups.reserve(materialRsrcs.size());

    for(const MaterialResource& mtlRsrc : materialRsrcs)
    {
        MLG_CHECKV(mtlRsrc.BaseTextureIndex == ResourceBundle::kInvalidIndex
                || mtlRsrc.BaseTextureIndex < textures.size(),
            "Invalid base texture index");

        wgpu::Texture baseTexture;
        std::string_view texturePath;
        if(mtlRsrc.BaseTextureIndex == ResourceBundle::kInvalidIndex)
        {
            baseTexture = gpuHelper.GetDefaultTexture();
            texturePath = "<default>";
        }
        else
        {
            const TextureResource& textureRsrc = textureRsrcs[mtlRsrc.BaseTextureIndex];
            baseTexture = textures[mtlRsrc.BaseTextureIndex];
            texturePath = resourceBundle.GetStringView(textureRsrc.TexturePath);
        }

        const ShaderInterop::MaterialConstants mc //
            {
                .Color = mtlRsrc.Color,
                .Metalness = mtlRsrc.Metalness,
                .Roughness = mtlRsrc.Roughness,
            };

        auto buffer =
            gpuHelper.CreateUniformBuffer<GpuMaterialConstantsBuffer>(1, "MaterialConstants");
        MLG_CHECK(buffer);

        buffer->Store(0, mc);

        auto bindGroup = gpuColorPass.CreateMaterialBindGroup(baseTexture, *buffer, texturePath);
        MLG_CHECK(bindGroup);

        materialBindGroups.push_back(std::move(*bindGroup));
    }

    return materialBindGroups;
}
} // namespace

View::View(const GpuHelper& gpuHelper,
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
    std::vector<wgpu::BindGroup>&& materialBindGroups)
    : m_GpuHelper(&gpuHelper),
      m_Level(&level),
      m_ColorPass(std::move(colorPass)),
      m_CompositorPass(std::move(compositorPass)),
      m_TransformPass(std::move(transformPass)),
      m_VertexBuffer(std::move(vertexBuffer)),
      m_IndexBuffer(std::move(indexBuffer)),
      m_WorldTransformBuffer(std::move(worldTransformBuffer)),
      m_ClipSpaceBuffer(std::move(clipSpaceBuffer)),
      m_MeshInstanceParamsBuffer(std::move(meshInstanceParamsBuffer)),
      m_CameraParamsBuffer(std::move(cameraParamsBuffer)),
      m_MaterialBindGroups(std::move(materialBindGroups))
{
    const size_t meshInstanceCount = CountMeshInstances(level.GetAllModelNodes());
    m_VisibleMeshes.reserve(meshInstanceCount);
}

Result<>
View::Render(const Camera& camera, const TrTransformf& cameraXForm)
{
    MLG_SCOPED_TIMER("View.Render");

    MLG_CHECK(SyncToGpu());

    const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "Renderer::Render" };
    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder(&encoderDesc);
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    MLG_CHECK(TransformNodes(gpuDevice, cmdEncoder, cameraXForm, camera));

    const Viewport& viewport = camera.GetViewport();

    if(!m_ColorPassOutputs
        || m_ColorPassOutputs->RenderTarget->GetWidth() != viewport.GetWidth()
        || m_ColorPassOutputs->RenderTarget->GetHeight() != viewport.GetHeight())
    {
        auto colorPassOutputs =
            CreateColorPassTarget(*m_GpuHelper, viewport.GetWidth(), viewport.GetHeight());
        MLG_CHECK(colorPassOutputs);

        m_ColorPassOutputs = std::move(*colorPassOutputs);
    }

    const GpuColorPass::Inputs colorPassInputs //
        {
            .Vertices = m_VertexBuffer,
            .Indices = m_IndexBuffer,
            .WorldTransforms = m_WorldTransformBuffer,
            .ClipSpaceTransforms = m_ClipSpaceBuffer,
            .MeshInstanceParams = m_MeshInstanceParamsBuffer,
            .CameraParams = m_CameraParamsBuffer,
        };

    MLG_CHECK(m_ColorPass.SetInputs(colorPassInputs));
    MLG_CHECK(m_ColorPass.SetOutputs(*m_ColorPassOutputs));

    auto invocation = m_ColorPass.Prepare(cmdEncoder);
    MLG_CHECK(invocation);

    m_VisibleMeshes.clear();
    const Frustum frustum(camera, cameraXForm);
    CollectVisibleMeshes(frustum, m_VisibleMeshes);
    std::ranges::sort(m_VisibleMeshes, {}, &MeshInstance::GetMaterialIndex);

    MLG_CHECK(invocation->Execute(viewport, m_VisibleMeshes, m_MaterialBindGroups));

    const wgpu::CommandBuffer cmdBuf = cmdEncoder.Finish(nullptr);
    MLG_CHECK(cmdBuf, "Failed to finish command buffer");

    const wgpu::Queue queue = gpuDevice.GetQueue();
    MLG_CHECK(queue, "Failed to get wgpu::Queue");

    queue.Submit(1, &cmdBuf);

    return Result<>::Ok;
}

Result<>
View::Composite(const GpuRenderTarget& target)
{
    const Rect dstRect(
        { .X = 0, .Y = 0, .Width = target->GetWidth(), .Height = target->GetHeight() });

    return Composite(target, dstRect);
}

Result<>
View::Composite(const GpuRenderTarget& target, const Rect& dstRect)
{
    MLG_CHECKV(m_ColorPassOutputs, "Color pass outputs are not valid");

    const GpuCompositorPass::Inputs inputs //
        {
            .DstRect = dstRect,
            .Texture = m_ColorPassOutputs->RenderTarget.Get(),
        };

    const GpuCompositorPass::Outputs outputs //
        {
            .RenderTarget = target,
        };

    MLG_CHECK(m_CompositorPass.SetInputs(inputs));
    MLG_CHECK(m_CompositorPass.SetOutputs(outputs));

    auto pass = m_CompositorPass.Prepare();
    MLG_CHECK(pass, "Failed to begin compositor pass");

    MLG_CHECK(pass->Execute(), "Failed to execute compositor pass");

    return Result<>::Ok;
}

// private:

void
View::CollectVisibleMeshes(const Frustum& frustum,
    std::vector<MeshInstance>& outVisibleMeshes) const
{
    static PerfCounter pcTotalMeshes({ .Name = "View.Meshes.Total" });
    static PerfCounter pcVisibleMeshes({ .Name = "View.Meshes.Visible" });

    outVisibleMeshes.clear();

    size_t totalMeshes = 0;

    for(const ModelNode& modelNode : m_Level->GetAllModelNodes())
    {
        totalMeshes += modelNode.GetMeshCount();

        if(!modelNode.IsVisible())
        {
            continue;
        }

        const BoundingSphere& modelBs =
            modelNode.GetWorldTransform() * modelNode.GetBoundingSphere();

        const Frustum::ContainsResult result = frustum.Contains(modelBs);

        if(result == Frustum::ContainsResult::Intersects)
        {
            // Model intersects frustum, check each mesh instance.

            for(const MeshInstance& meshInstance : modelNode.GetMeshes())
            {
                const BoundingSphere meshBs =
                    modelNode.GetWorldTransform() * meshInstance.GetBoundingSphere();

                if(Frustum::ContainsResult::Outside != frustum.Contains(meshBs))
                {
                    outVisibleMeshes.push_back(meshInstance);
                }
            }
        }
        else if(result == Frustum::ContainsResult::Inside)
        {
            // Model is fully inside frustum, add all mesh instances.

            for(const MeshInstance& meshInstance : modelNode.GetMeshes())
            {
                outVisibleMeshes.push_back(meshInstance);
            }
        }
        else
        {
            // Model is fully outside frustum, skip all mesh instances.
            continue;
        }
    }

    pcTotalMeshes.Increment(totalMeshes);
    pcVisibleMeshes.Increment(outVisibleMeshes.size());
}

Result<>
View::SyncToGpu()
{
    // Brute force copy everything for now.
    uint64_t bufferOffset = 0;
    for(const ModelNode& modelNode : m_Level->GetAllModelNodes())
    {
        const ShaderInterop::WorldTransform transform{ .Transform = modelNode.GetWorldTransform() };
        m_GpuHelper->GetDevice().GetQueue().WriteBuffer(m_WorldTransformBuffer.GetGpuBuffer(),
            bufferOffset,
            &transform,
            sizeof(transform));

        bufferOffset += sizeof(transform);
    }

    return Result<>::Ok;
}

Result<>
View::TransformNodes(const wgpu::Device& gpuDevice,
    const wgpu::CommandEncoder& cmdEncoder,
    const TrTransformf& cameraXForm,
    const Camera& camera)
{
    // Use inverse of camera transform as view matrix
    const Mat44f viewMat = cameraXForm.Inverse().ToMatrix();
    const Mat44f& projMat = camera.GetProjectionMatrix();
    const Mat44f viewProjMat = projMat.Mul(viewMat);

    const ShaderInterop::CameraParams cameraParams //
        {
            .View = viewMat,
            .Projection = projMat,
            .ViewProj = viewProjMat,
        };

    gpuDevice.GetQueue().WriteBuffer(m_CameraParamsBuffer.GetGpuBuffer(),
        0,
        &cameraParams,
        sizeof(ShaderInterop::CameraParams));

    const GpuTransformPass::Inputs inputs //
        {
            .WorldTransforms = m_WorldTransformBuffer,
            .CameraParams = m_CameraParamsBuffer,
        };

    const GpuTransformPass::Outputs outputs //
        {
            .ClipSpaceTransforms = m_ClipSpaceBuffer,
        };

    MLG_CHECK(m_TransformPass.SetInputs(inputs));
    MLG_CHECK(m_TransformPass.SetOutputs(outputs));
    auto invocation = m_TransformPass.Prepare(cmdEncoder);
    MLG_CHECK(invocation, "Failed to prepare transform pass");

    MLG_CHECK(invocation->Execute(), "Failed to execute transform pass");

    return Result<>::Ok;
}

namespace
{
Result<std::vector<RelativeFilePath>>
GetTexturePaths(const ResourceBundle& resourceBundle)
{
    const std::span texResources = resourceBundle.GetTextures();
    std::vector<RelativeFilePath> texturePaths;
    texturePaths.reserve(texResources.size());
    for(const auto& texRsrc : texResources)
    {
        const std::string_view sv = resourceBundle.GetStringView(texRsrc.TexturePath);
        auto path = RelativeFilePath::Create(sv);
        MLG_CHECK(path, "Failed to create FilePath");

        texturePaths.emplace_back(std::move(*path));
    }
    return texturePaths;
}
} // namespace

// View::CreateTask
View::CreateTask::CreateTask(System& system,
    const DirectoryPath& parentPath,
    const ResourceBundle& resourceBundle,
    const Level& level)
    : m_System(&system),
      m_ResourceBundle(&resourceBundle),
      m_Level(&level),
      m_ParentPath(parentPath),
      m_ColorPassTask(m_System->GetGpuHelper(), m_System->GetFileFetcher()),
      m_CompositorPassTask(m_System->GetGpuHelper(), m_System->GetFileFetcher()),
      m_TransformPassTask(m_System->GetGpuHelper(), m_System->GetFileFetcher())
{
}

Result<std::unique_ptr<View>>
View::CreateTask::Take()
{
    MLG_CHECKV(Stage::Succeeded == m_Stage, "Task did not succeed");
    MLG_CHECKV(!m_Consumed, "Task result already consumed");

    m_Consumed = true;

    MLG_CHECKV(m_TextureFetcher, "Texture fetcher is not valid");
    auto textures = m_TextureFetcher->Take();
    MLG_CHECK(textures, "Failed to fetch textures");

    auto gpuColorPassResult = m_ColorPassTask.Take();
    MLG_CHECK(gpuColorPassResult, "Failed to create GpuColorPass");

    auto gpuCompositorPassResult = m_CompositorPassTask.Take();
    MLG_CHECK(gpuCompositorPassResult, "Failed to create GpuCompositorPass");

    auto gpuTransformPassResult = m_TransformPassTask.Take();
    MLG_CHECK(gpuTransformPassResult, "Failed to create GpuTransformPass");

    const GpuHelper& gpuHelper = m_System->GetGpuHelper();

    auto materialBindGroups =
        CreateMaterialBindGroups(gpuHelper, *gpuColorPassResult, *m_ResourceBundle, *textures);
    MLG_CHECK(materialBindGroups);

    const std::span vertices = m_ResourceBundle->GetVertices();
    auto vertexBuffer = gpuHelper.CreateVertexBuffer(vertices.size(), "VertexBuffer");
    MLG_CHECK(vertexBuffer);
    vertexBuffer->Store(vertices);

    const std::span indices = m_ResourceBundle->GetIndices();
    auto indexBuffer = gpuHelper.CreateIndexBuffer(indices.size(), "IndexBuffer");
    MLG_CHECK(indexBuffer);
    indexBuffer->Store(indices);

    const std::span modelNodes = m_Level->GetAllModelNodes();

    auto transformBuffer = gpuHelper.CreateStorageBuffer<GpuWorldTransformBuffer>(modelNodes.size(),
        "WorldTransforms");
    MLG_CHECK(transformBuffer);

    auto clipSpaceBuffer =
        gpuHelper.CreateStorageBuffer<GpuClipSpaceBuffer>(modelNodes.size(), "ClipSpaceTransforms");
    MLG_CHECK(clipSpaceBuffer);

    auto meshInstanceParamsBuffer = BuildMeshInstanceParamsBuffer(gpuHelper, modelNodes);
    MLG_CHECK(meshInstanceParamsBuffer);

    auto cameraParamsBuf = gpuHelper.CreateUniformBuffer<GpuCameraParamsBuffer>(1, "CameraParams");
    MLG_CHECK(cameraParamsBuf);

    std::unique_ptr<View> view(new View(gpuHelper,
        *m_Level,
        std::move(*gpuColorPassResult),
        std::move(*gpuCompositorPassResult),
        std::move(*gpuTransformPassResult),
        std::move(*vertexBuffer),
        std::move(*indexBuffer),
        std::move(*transformBuffer),
        std::move(*clipSpaceBuffer),
        std::move(*meshInstanceParamsBuffer),
        std::move(*cameraParamsBuf),
        std::move(*materialBindGroups)));

    MLG_CHECK(view->SyncToGpu());

    return view;
}

// private:

Result<>
View::CreateTask::OnStart()
{
    m_Timer.Start();

    m_Stage = Stage::Failed;

    auto texturePaths = GetTexturePaths(*m_ResourceBundle);
    MLG_CHECK(texturePaths, "Failed to get texture paths");

    m_TextureFetcher.emplace(m_System->GetGpuHelper(),
        m_System->GetFileFetcher(),
        m_System->GetThreadPool(),
        m_ParentPath,
        std::move(*texturePaths));

    ICoopTask<>* tasks[] =//
        {
            &m_TextureFetcher.value(),
            &m_ColorPassTask,
            &m_CompositorPassTask,
            &m_TransformPassTask,
        };

    m_TaskBatch.emplace(tasks);

    MLG_CHECK(m_TaskBatch->Start(), "Failed to begin task batch");

    m_Stage = Stage::Running;

    return Result<>::Ok;
}
void
View::CreateTask::OnUpdate()
{
    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::Running:
            if(!MLG_VERIFY(m_TaskBatch, "Task batch is not valid"))
            {
                m_Stage = Stage::Failed;
            }
            else if(m_TaskBatch->IsRunning())
            {
                m_System->GetFileFetcher().ProcessCompletions();
                m_TaskBatch->Update();
            }
            else
            {
                m_Stage = Stage::Succeeded;
            }
            break;
        case Stage::Failed:
            MLG_ERROR("View creation failed");
            [[fallthrough]];
        case Stage::Succeeded:
            m_Timer.Stop();
            MLG_INFO("View created in {} ms", m_Timer.GetElapsedSeconds() * 1000);
            SetComplete();
            break;
    }
}