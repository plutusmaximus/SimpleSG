#define MLG_LOGGER_NAME "SCEN"

#include "Scene.h"

#include "Camera.h"
#include "GpuHelper.h"
#include "PerfMetrics.h"
#include "ResourceBundle.h"
#include "TextureFetcher.h"
#include "Timer.h"

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
    const std::span<const MaterialResource> materialRsrcs,
    const std::span<const wgpu::Texture> textures,
    const std::span<const std::string> textureUris)
{
    std::vector<wgpu::BindGroup> materialBindGroups;
    materialBindGroups.reserve(materialRsrcs.size());

    for(const MaterialResource& mtlRsrc : materialRsrcs)
    {
        MLG_CHECKV(mtlRsrc.BaseTextureIndex == Resource::kInvalidIndex
                || mtlRsrc.BaseTextureIndex < textures.size(),
            "Invalid base texture index");

        wgpu::Texture baseTexture;
        std::string_view textureUri;
        if(mtlRsrc.BaseTextureIndex == Resource::kInvalidIndex)
        {
            baseTexture = gpuHelper.GetDefaultTexture();
            textureUri = "<default>";
        }
        else
        {
            baseTexture = textures[mtlRsrc.BaseTextureIndex];
            textureUri = textureUris[mtlRsrc.BaseTextureIndex];
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

        auto bindGroup = gpuColorPass.CreateMaterialBindGroup(baseTexture, *buffer, textureUri);
        MLG_CHECK(bindGroup);

        materialBindGroups.push_back(std::move(*bindGroup));
    }

    return materialBindGroups;
}
} // namespace

Result<Scene>
Scene::Create(const GpuHelper& gpuHelper,
    ThreadPool& threadPool,
    FileFetcher& fileFetcher,
    const std::filesystem::path& rootPath,
    const ResourceBundle& resourceBundle,
    const std::span<const ModelNode> modelNodes)
{
    Timer createTimer;
    createTimer.Start();

    const std::span textureUriStrings = resourceBundle.GetTextureUris();
    std::vector<std::string> textureUris;
    textureUris.reserve(textureUriStrings.size());
    for(const auto& uri : textureUriStrings)
    {
        textureUris.emplace_back(resourceBundle.GetString(uri));
    }

    auto textureFetcher =
        TextureFetcher::Create(gpuHelper, threadPool, fileFetcher, rootPath, textureUris);
    MLG_CHECK(textureFetcher, "Failed to create TextureFetcher");

    while(!textureFetcher->IsComplete())
    {
        textureFetcher->Update();
    }

    MLG_CHECK(textureFetcher->Succeeded(), "Failed to fetch textures");
    auto textures = textureFetcher->Take();
    MLG_CHECK(textures);

    auto gpuColorPassResult = GpuColorPass::Create(gpuHelper, fileFetcher);
    MLG_CHECK(gpuColorPassResult, "Failed to create GpuColorPass");

    auto gpuCompositorPassResult = GpuCompositorPass::Create(gpuHelper, fileFetcher);
    MLG_CHECK(gpuCompositorPassResult, "Failed to create GpuCompositorPass");

    auto gpuTransformPassResult = GpuTransformPass::Create(gpuHelper, fileFetcher);
    MLG_CHECK(gpuTransformPassResult, "Failed to create GpuTransformPass");

    auto materialBindGroups = CreateMaterialBindGroups(gpuHelper,
        *gpuColorPassResult,
        resourceBundle.GetMaterials(),
        *textures,
        textureUris);
    MLG_CHECK(materialBindGroups);

    const std::span vertices = resourceBundle.GetVertices();
    auto vertexBuffer = gpuHelper.CreateVertexBuffer(vertices.size(), "VertexBuffer");
    MLG_CHECK(vertexBuffer);
    vertexBuffer->Store(vertices);

    const std::span indices = resourceBundle.GetIndices();
    auto indexBuffer = gpuHelper.CreateIndexBuffer(indices.size(), "IndexBuffer");
    MLG_CHECK(indexBuffer);
    indexBuffer->Store(indices);

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

    Scene scene(gpuHelper,
        modelNodes,
        std::move(*gpuColorPassResult),
        std::move(*gpuCompositorPassResult),
        std::move(*gpuTransformPassResult),
        std::move(*vertexBuffer),
        std::move(*indexBuffer),
        std::move(*transformBuffer),
        std::move(*clipSpaceBuffer),
        std::move(*meshInstanceParamsBuffer),
        std::move(*cameraParamsBuf),
        std::move(*materialBindGroups));

    MLG_CHECK(scene.SyncToGpu());

    MLG_INFO("Scene created in {} ms", createTimer.GetElapsedSeconds() * 1000);

    return std::move(scene);
}

Scene::Scene(const GpuHelper& gpuHelper,
    const std::span<const ModelNode> modelNodes,
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
      m_ModelNodes(modelNodes),
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
    const size_t meshInstanceCount = CountMeshInstances(m_ModelNodes);
    m_VisibleMeshes.reserve(meshInstanceCount);
}

Result<>
Scene::Render(const Camera& camera, const TrTransformf& cameraXForm)
{
    MLG_SCOPED_TIMER("Scene.Render");

    MLG_CHECK(SyncToGpu());

    const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "Renderer::Render" };
    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder(&encoderDesc);
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    auto transformNodesResult = TransformNodes(gpuDevice, cmdEncoder, cameraXForm, camera);
    MLG_CHECK(transformNodesResult);

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
            .Viewport = viewport,
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

    MLG_CHECK(invocation->Execute(m_VisibleMeshes, m_MaterialBindGroups));

    const wgpu::CommandBuffer cmdBuf = cmdEncoder.Finish(nullptr);
    MLG_CHECK(cmdBuf, "Failed to finish command buffer");

    const wgpu::Queue queue = gpuDevice.GetQueue();
    MLG_CHECK(queue, "Failed to get wgpu::Queue");

    queue.Submit(1, &cmdBuf);

    return Result<>::Ok;
}

Result<>
Scene::Composite(const GpuRenderTarget& target)
{
    const Rect dstRect(
        { .X = 0, .Y = 0, .Width = target->GetWidth(), .Height = target->GetHeight() });

    return Composite(target, dstRect);
}

Result<>
Scene::Composite(const GpuRenderTarget& target, const Rect& dstRect)
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
Scene::CollectVisibleMeshes(const Frustum& frustum,
    std::vector<MeshInstance>& outVisibleMeshes) const
{
    static PerfCounter pcTotalMeshes({ .Name = "Scene.Meshes.Total" });
    static PerfCounter pcVisibleMeshes({ .Name = "Scene.Meshes.Visible" });

    outVisibleMeshes.clear();

    size_t totalMeshes = 0;

    for(const ModelNode& modelNode : m_ModelNodes)
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
Scene::SyncToGpu()
{
    // Brute force copy everything for now.
    uint64_t bufferOffset = 0;
    for(const ModelNode& modelNode : m_ModelNodes)
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
Scene::TransformNodes(const wgpu::Device& gpuDevice,
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