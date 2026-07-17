#define MLG_LOGGER_NAME "RNDR"

#include "Renderer.h"

#include "Camera.h"
#include "GpuColorPass.h"
#include "GpuHelper.h"
#include "narrow_cast.h"
#include "PerfMetrics.h"
#include "PropKit.h"
#include "Scene.h"
#include "shaders/ShaderInterop.h"

namespace
{

Result<GpuColorPass::Outputs>
CreateColorPassTarget(const GpuHelper& gpuHelper, const uint32_t width, const uint32_t height)
{
    MLG_DEBUG("Creating new color/depth target with size {}x{}", width, height);

    auto renderTarget = gpuHelper.CreateRenderTarget(width, height, "ColorPass::RenderTarget");
    MLG_CHECK(renderTarget);

    auto depthBuffer = gpuHelper.CreateDepthBuffer(width, height, "ColorPass::DepthBuffer");
    MLG_CHECK(depthBuffer);

    return GpuColorPass::Outputs //
        {
            .RenderTarget = *renderTarget,
            .DepthBuffer = *depthBuffer,
        };
}

} // namespace

Result<std::unique_ptr<Renderer>>
Renderer::Create(GpuHelper& gpuHelper, FileFetcher& fileFetcher)
{
    auto gpuColorPassResult = GpuColorPass::Create(gpuHelper, fileFetcher);
    MLG_CHECK(gpuColorPassResult, "Failed to create GpuColorPass");

    auto gpuCompositorPassResult = GpuCompositorPass::Create(gpuHelper, fileFetcher);
    MLG_CHECK(gpuCompositorPassResult, "Failed to create GpuCompositorPass");

    auto gpuTransformPassResult = GpuTransformPass::Create(gpuHelper, fileFetcher);
    MLG_CHECK(gpuTransformPassResult, "Failed to create GpuTransformPass");

    return std::unique_ptr<Renderer>(new Renderer(gpuHelper,
        std::move(*gpuColorPassResult),
        std::move(*gpuCompositorPassResult),
        std::move(*gpuTransformPassResult)));
}

Result<>
Renderer::Render(const Camera& camera,
    const TrTransformf& cameraXForm,
    const Scene& scene,
    const PropKit& propKit)
{
    const Viewport& viewport = camera.GetViewport();

    MLG_SCOPED_TIMER("Renderer.Render");

    const wgpu::Device& gpuDevice = m_GpuHelper->GetDevice();

    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "Renderer::Render" };

    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder(&encoderDesc);
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    {
        MLG_SCOPED_TIMER("Renderer.Render.TransformNodes");

        auto transformNodesResult = TransformNodes(gpuDevice, cmdEncoder, cameraXForm, camera, scene);
        MLG_CHECK(transformNodesResult);
    }

    if(!m_ColorPassOutputs ||
        m_ColorPassOutputs->RenderTarget->GetWidth() != viewport.GetWidth() ||
        m_ColorPassOutputs->RenderTarget->GetHeight() != viewport.GetHeight())
    {
        auto colorPassOutputs =
            CreateColorPassTarget(*m_GpuHelper, viewport.GetWidth(), viewport.GetHeight());
        MLG_CHECK(colorPassOutputs);

        m_ColorPassOutputs = std::move(*colorPassOutputs);
    }

    const GpuColorPass::Inputs colorPassInputs //
        {
            .Viewport = viewport,
            .Vertices = propKit.GetVertexBuffer(),
            .Indices = propKit.GetIndexBuffer(),
            .WorldTransforms = scene.GetWorldTransformBuffer(),
            .ClipSpaceTransforms = scene.GetClipSpaceBuffer(),
            .MeshProperties = scene.GetMeshPropertiesBuffer(),
            .MaterialConstants = propKit.GetMaterialConstants(),
            .CameraParams = scene.GetCameraParamsBuffer(),
        };

    MLG_CHECK(m_ColorPass.SetInputs(colorPassInputs));
    MLG_CHECK(m_ColorPass.SetOutputs(*m_ColorPassOutputs));

    wgpu::RenderPassEncoder renderPass;
    {
        MLG_SCOPED_TIMER("Renderer.Render.BeginRenderPass");
        auto renderPassResult = m_ColorPass.BeginPass(cmdEncoder);
        MLG_CHECK(renderPassResult);

        renderPass = *renderPassResult;
    }

    {
        MLG_SCOPED_TIMER("Renderer.Render.Draw")

        const auto& drawIndirectBuffer = scene.GetDrawIndirectBuffer();
        const Frustum frustum(camera, cameraXForm);

        MaterialIdentifier lastMaterialId{};

        // Get visible meshes and sort by material to minimize bind group changes.
        scene.GetVisibleMeshes(frustum, m_VisibleMeshes);

        std::ranges::sort(m_VisibleMeshes, {}, &MeshInstance::GetMaterialId);

        // Track how many times we have to change materials.
        static PerfCounter pcMaterialChanges({ .Name = "Renderer.Render.MaterialChanges" });

        for(const MeshInstance& meshInstance : m_VisibleMeshes)
        {
            const Mesh& mesh = meshInstance.GetMesh();
            const MaterialIdentifier materialId = mesh.GetMaterialId();

            if(materialId != lastMaterialId)
            {
                MLG_SCOPED_TIMER("Renderer.Render.Draw.SetMaterialBindGroup");

                pcMaterialChanges.Increment(1);

                const wgpu::BindGroup* bindGroup = propKit.GetMaterialBindGroup(materialId);
                MLG_CHECKV(bindGroup, "Failed to get material bind group");

                renderPass.SetBindGroup(1, *bindGroup, 0, nullptr);
                lastMaterialId = materialId;
            }

            {
                MLG_SCOPED_TIMER("Renderer.Render.Draw.DrawIndexed");

                const uint64_t indirectOffset =
                    meshInstance.GetInstanceIndex() * sizeof(ShaderInterop::DrawIndirectParams);
                renderPass.DrawIndexedIndirect(drawIndirectBuffer.GetGpuBuffer(), indirectOffset);
            }
        }
    }

    renderPass.End();

    wgpu::CommandBuffer cmdBuf;
    {
        MLG_SCOPED_TIMER("Renderer.FinishCommandBuffer");
        cmdBuf = cmdEncoder.Finish(nullptr);

        MLG_CHECK(cmdBuf, "Failed to finish command buffer");
    }

    {
        MLG_SCOPED_TIMER("Renderer.SubmitCommandBuffer");
        const wgpu::Queue queue = gpuDevice.GetQueue();
        MLG_CHECK(queue, "Failed to get wgpu::Queue");

        queue.Submit(1, &cmdBuf);
    }

    return Result<>::Ok;
}

Result<>
Renderer::Composite(const ValidTexture& target)
{
    const Rect dstRect
        ({ .X = 0, .Y = 0, .Width = target->GetWidth(), .Height = target->GetHeight() });
        
    return Composite(target, dstRect);
}

Result<>
Renderer::Composite(const ValidTexture& target, const Rect& dstRect)
{
    MLG_CHECKV(m_ColorPassOutputs, "Color pass outputs are not valid");
    
    const GpuCompositorPass::Inputs inputs //
        {
            .DstRect = dstRect,
            .Texture = m_ColorPassOutputs->RenderTarget,
        };

    const GpuCompositorPass::Outputs outputs //
        {
            .Texture = target,
        };

    MLG_CHECK(m_CompositorPass.SetInputs(inputs));
    MLG_CHECK(m_CompositorPass.SetOutputs(outputs));

    return m_CompositorPass.Composite();
}

//private:

Result<>
Renderer::TransformNodes(const wgpu::Device& gpuDevice,
    const wgpu::CommandEncoder& cmdEncoder,
    const TrTransformf& cameraXForm,
    const Camera& camera,
    const Scene& scene)
{
    // Use inverse of camera transform as view matrix
    const Mat44f viewMat = cameraXForm.Inverse().ToMatrix();
    const Mat44f& projMat = camera.GetMatrix();
    const Mat44f viewProjMat = projMat.Mul(viewMat);

    const ShaderInterop::CameraParams cameraParams //
        {
            .View = viewMat,
            .Projection = projMat,
            .ViewProj = viewProjMat,
        };

    auto cameraParamsBuf = scene.GetCameraParamsBuffer();

    gpuDevice.GetQueue().WriteBuffer(
        cameraParamsBuf.GetGpuBuffer(),
        0,
        &cameraParams,
        sizeof(ShaderInterop::CameraParams));

    const GpuTransformPass::Inputs inputs //
        {
            .WorldTransforms = scene.GetWorldTransformBuffer(),
            .CameraParams = cameraParamsBuf,
        };

    const GpuTransformPass::Outputs outputs //
        {
            .ClipSpaceTransforms = scene.GetClipSpaceBuffer(),
        };

    MLG_CHECK(m_TransformPass.SetInputs(inputs));
    MLG_CHECK(m_TransformPass.SetOutputs(outputs));
    auto pass = m_TransformPass.BeginPass(cmdEncoder);
    MLG_CHECK(pass);

    const uint32_t workgroupCountX = narrow_cast<uint32_t>(scene.GetModelInstances().size());
    pass->DispatchWorkgroups(workgroupCountX);
    pass->End();

    return Result<>::Ok;
}