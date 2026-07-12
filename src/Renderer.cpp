#define MLG_LOGGER_NAME "RNDR"

#include "Renderer.h"

#include "Camera.h"
#include "FileFetcher.h"
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

Result<>
Renderer::Startup(GpuHelper& gpuHelper, FileFetcher& fileFetcher)
{
    MLG_CHECKV(!m_Initialized, "Renderer is already initialized");

    gpuHelper.GetDevice().GetLimits(&m_GpuLimits);

    auto gpuColorPassResult = GpuColorPass::Create(gpuHelper, fileFetcher);
    MLG_CHECK(gpuColorPassResult);

    auto gpuCompositorPassResult = GpuCompositorPass::Create(gpuHelper, fileFetcher);
    MLG_CHECK(gpuCompositorPassResult);

    auto gpuTransformPassResult = GpuTransformPass::Create(gpuHelper, fileFetcher);
    MLG_CHECK(gpuTransformPassResult);

    m_ColorPass = std::move(*gpuColorPassResult);
    m_CompositorPass = std::move(*gpuCompositorPassResult);
    m_TransformPass = std::move(*gpuTransformPassResult);

    m_Initialized = true;

    return Result<>::Ok;
}

Result<>
Renderer::Shutdown()
{
    if(!m_Initialized)
    {
        // Not initialized, nothing to do
        return Result<>::Ok;
    }

    m_Initialized = false;

    return Result<>::Ok;
}

Result<>
Renderer::Render(const GpuHelper& gpuHelper,
    const Camera& camera,
    const TrTransformf& cameraXForm,
    const Scene& scene,
    const PropKit& propKit)
{
    MLG_CHECKV(m_Initialized, "Renderer is not initialized");

    const Viewport& viewport = camera.GetViewport();

    MLG_SCOPED_TIMER("Renderer.Render");

    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "Renderer::Render" };

    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder(&encoderDesc);
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    {
        MLG_SCOPED_TIMER("Renderer.Render.TransformNodes");

        auto transformNodesResult = TransformNodes(gpuHelper, cmdEncoder, cameraXForm, camera, scene);
        MLG_CHECK(transformNodesResult);
    }

    if(!m_ColorPassOutputs.RenderTarget ||
        m_ColorPassOutputs.RenderTarget.GetWidth() != viewport.GetWidth() ||
        m_ColorPassOutputs.RenderTarget.GetHeight() != viewport.GetHeight())
    {
        auto colorPassOutputs =
            CreateColorPassTarget(gpuHelper, viewport.GetWidth(), viewport.GetHeight());
        MLG_CHECK(colorPassOutputs);

        m_ColorPassOutputs = std::move(*colorPassOutputs);
    }

    const GpuColorPass::Inputs colorPassInputs //
        {
            .Vertices = propKit.GetVertexBuffer(),
            .Indices = propKit.GetIndexBuffer(),
            .WorldTransforms = scene.GetWorldTransformBuffer(),
            .ClipSpaceTransforms = scene.GetClipSpaceBuffer(),
            .MeshProperties = scene.GetMeshPropertiesBuffer(),
            .MaterialConstants = propKit.GetMaterialConstants(),
            .CameraParams = scene.GetCameraParamsBuffer(),
        };

    MLG_CHECKV(m_ColorPass, "Color pass is not initialized");

    MLG_CHECK(m_ColorPass->SetInputs(gpuHelper, colorPassInputs));
    MLG_CHECK(m_ColorPass->SetOutputs(gpuHelper, m_ColorPassOutputs));

    wgpu::RenderPassEncoder renderPass;
    {
        MLG_SCOPED_TIMER("Renderer.Render.BeginRenderPass");
        auto renderPassResult = m_ColorPass->BeginPass(cmdEncoder);
        MLG_CHECK(renderPassResult);

        renderPass = *renderPassResult;
    }

    renderPass.SetViewport(static_cast<float>(viewport.GetX()),
        static_cast<float>(viewport.GetY()),
        static_cast<float>(viewport.GetWidth()),
        static_cast<float>(viewport.GetHeight()),
        viewport.GetMinDepth(),
        viewport.GetMaxDepth());

    renderPass.SetScissorRect(viewport.GetX(),
        viewport.GetY(),
        viewport.GetWidth(),
        viewport.GetHeight());

    {
        MLG_SCOPED_TIMER("Renderer.Render.Draw.SetBuffers");

        constexpr size_t kU16BitWidth = 16;
        constexpr size_t kU32BitWidth = 32;

        static_assert(VERTEX_INDEX_BITS == kU32BitWidth || VERTEX_INDEX_BITS == kU16BitWidth,
            "Unsupported index buffer format: only 16-bit and 32-bit indices are supported");

        constexpr wgpu::IndexFormat idxFmt =
            (VERTEX_INDEX_BITS == kU32BitWidth)
            ? wgpu::IndexFormat::Uint32
            : wgpu::IndexFormat::Uint16;

        renderPass.SetVertexBuffer(0,
            propKit.GetVertexBuffer().GetGpuBuffer(),
            0,
            propKit.GetVertexBuffer().BufferSize());

        renderPass.SetIndexBuffer(propKit.GetIndexBuffer().GetGpuBuffer(),
            idxFmt,
            0,
            propKit.GetIndexBuffer().BufferSize());
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
Renderer::Composite(GpuHelper& gpuHelper, const wgpu::Texture& target)
{
    const Rect dstRect
        ({ .X = 0, .Y = 0, .Width = target.GetWidth(), .Height = target.GetHeight() });
        
    return Composite(gpuHelper, target, dstRect);
}

Result<>
Renderer::Composite(GpuHelper& gpuHelper, const wgpu::Texture& target, const Rect& dstRect)
{
    MLG_CHECKV(m_Initialized, "Renderer is not initialized");
    MLG_CHECKV(m_CompositorPass, "Compositor pass is not initialized");

    const GpuCompositorPass::Inputs inputs //
        {
            .DstRect = dstRect,
            .Texture = m_ColorPassOutputs.RenderTarget,
        };

    const GpuCompositorPass::Outputs outputs //
        {
            .Texture = target,
        };

    MLG_CHECK(m_CompositorPass->SetInputs(gpuHelper, inputs));
    MLG_CHECK(m_CompositorPass->SetOutputs(gpuHelper, outputs));

    return m_CompositorPass->Composite(gpuHelper);
}

Result<wgpu::Texture>
Renderer::GetTarget() const
{
    MLG_CHECKV(m_Initialized, "Renderer is not initialized");

    return m_ColorPassOutputs.RenderTarget;
}

//private:

Result<>
Renderer::TransformNodes(const GpuHelper& gpuHelper,
    const wgpu::CommandEncoder& cmdEncoder,
    const TrTransformf& cameraXForm,
    const Camera& camera,
    const Scene& scene)
{
    MLG_CHECKV(m_TransformPass, "Transform pass is not initialized");

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

    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

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

    MLG_CHECK(m_TransformPass->SetInputs(gpuHelper, inputs));
    MLG_CHECK(m_TransformPass->SetOutputs(gpuHelper, outputs));
    auto pass = m_TransformPass->BeginPass(cmdEncoder);
    MLG_CHECK(pass);

    const uint32_t workgroupCountX = narrow_cast<uint32_t>(scene.GetModelInstances().size());
    pass->DispatchWorkgroups(workgroupCountX);
    pass->End();

    return Result<>::Ok;
}