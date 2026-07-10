#define MLG_LOGGER_NAME "RNDR"

#include "Renderer.h"

#include "Camera.h"
#include "FileFetcher.h"
#include "GpuColorPass.h"
#include "GpuHelper.h"
#include "GpuLayouts.h"
#include "narrow_cast.h"
#include "PerfMetrics.h"
#include "PropKit.h"
#include "Scene.h"
#include "shaders/TransformShaderContract.h"
#include "shaders/ShaderInterop.h"

#include <thread>

namespace
{
Result<>
LoadShaderCode(const char* filePath, std::vector<uint8_t>& outBuffer, FileFetcher& fileFetcher)
{
    FileFetcher::Request request(filePath);
    MLG_CHECK(fileFetcher.Fetch(request));

    while(request.IsPending())
    {
        MLG_CHECK(fileFetcher.ProcessCompletions());
        std::this_thread::yield();
    }

    MLG_CHECK(request.Succeeded(), "Failed to load shader file: {}", filePath);

    request.MoveDataTo(outBuffer);

    return Result<>::Ok;
}

Result<wgpu::ShaderModule>
CreateShader(const wgpu::Device& gpuDevice, const char* path, FileFetcher& fileFetcher)
{
    std::vector<uint8_t> shaderCode;
    auto loadResult = LoadShaderCode(path, shaderCode, fileFetcher);
    MLG_CHECK(loadResult);

    const void* data = shaderCode.data();
    const wgpu::StringView shaderCodeView{ static_cast<const char*>(data), shaderCode.size() };
    const wgpu::ShaderSourceWGSL wgsl{ { .nextInChain = nullptr, .code = shaderCodeView } };
    const wgpu::ShaderModuleDescriptor shaderModuleDescriptor{ .nextInChain = &wgsl, .label = path };

    wgpu::ShaderModule shaderModule = gpuDevice.CreateShaderModule(&shaderModuleDescriptor);
    MLG_CHECK(shaderModule, "Failed to create shader module");

    return shaderModule;
}
}

Result<>
Renderer::Startup(GpuHelper& gpuHelper, FileFetcher& fileFetcher)
{
    MLG_CHECKV(!m_Initialized, "Renderer is already initialized");

    gpuHelper.GetDevice().GetLimits(&m_GpuLimits);

    auto gpuColorPassResult = GpuColorPass::Create(gpuHelper, fileFetcher);
    MLG_CHECK(gpuColorPassResult);
    m_ColorPass = std::move(*gpuColorPassResult);

    MLG_CHECK(CreateTransformPipeline(gpuHelper.GetDevice(), fileFetcher));

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

    m_TransformPipelineResources = {};
    m_TransformPipeline = {};

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
    MLG_CHECKV(m_ColorPass, "Color pass is not initialized");

    const Viewport& viewport = camera.GetViewport();

    MLG_SCOPED_TIMER("Renderer.Render");

    if(!m_TargetResources.Target ||
        m_TargetResources.Target.GetWidth() != viewport.GetWidth() ||
        m_TargetResources.Target.GetHeight() != viewport.GetHeight())
    {
        auto colorTargetResources = GpuColorPass::CreateTarget(gpuHelper.GetDevice(),
            viewport.GetWidth(),
            viewport.GetHeight());

        m_TargetResources = std::move(*colorTargetResources);
    }

    const GpuColorPass::Resources colorPassResources //
        {
            .Vertices = propKit.GetVertexBuffer(),
            .Indices = propKit.GetIndexBuffer(),
            .WorldTransforms = scene.m_WorldTransformBuffer,
            .ClipSpaceTransforms = scene.m_ClipSpaceBuffer,
            .MeshProperties = scene.m_MeshPropertiesBuffer,
            .MaterialConstants = propKit.GetMaterialConstants(),
            .CameraParams = scene.m_CameraParamsBuffer,
        };

    m_ColorPass->BindResources(gpuHelper, colorPassResources, m_TargetResources);

    const wgpu::Device& gpuDevice = gpuHelper.GetDevice();

    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "Renderer::Render" };

    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder(&encoderDesc);
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    {
        MLG_SCOPED_TIMER("Renderer.Render.TransformNodes");

        auto transformNodesResult = TransformNodes(gpuDevice, cmdEncoder, cameraXForm, camera, scene);
        MLG_CHECK(transformNodesResult);
    }

    wgpu::RenderPassEncoder renderPass;
    {
        MLG_SCOPED_TIMER("Renderer.Render.BeginRenderPass");
        auto renderPassResult = m_ColorPass->BeginRenderPass(cmdEncoder);
        //auto renderPassResult = BeginRenderPass(cmdEncoder);
        MLG_CHECK(renderPassResult);

        renderPass = *renderPassResult;
    }

    renderPass.SetViewport(static_cast<float>(viewport.GetX()),
        static_cast<float>(viewport.GetY()),
        static_cast<float>(viewport.GetWidth()),
        static_cast<float>(viewport.GetHeight()),
        viewport.GetMinDepth(),
        viewport.GetMaxDepth());

    renderPass.SetScissorRect(0, 0, viewport.GetWidth(), viewport.GetHeight());

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
Renderer::Composite(const wgpu::Device& gpuDevice, const wgpu::Texture& target) const
{
    MLG_CHECKV(m_Initialized, "Renderer is not initialized");
    MLG_CHECKV(m_ColorPass, "Color pass is not initialized");

    return m_ColorPass->Composite(gpuDevice, target);
}

Result<wgpu::Texture>
Renderer::GetTarget() const
{
    MLG_CHECKV(m_Initialized, "Renderer is not initialized");

    return m_TargetResources.Target;
}

//private:

Result<>
Renderer::CreateTransformPipeline(const wgpu::Device& gpuDevice, FileFetcher& fileFetcher)
{
    if(m_TransformPipeline)
    {
        return Result<>::Ok;
    }

    auto csResult = CreateShader(gpuDevice,
        TransformShaderContract::GetShaderPath(),
        fileFetcher);
    MLG_CHECK(csResult);

    m_TransformPipelineResources.Shader = *csResult;

    auto layout =
        GpuLayouts::GetOrCreateLayout<TransformShaderContract::SceneGroup>(gpuDevice);
    MLG_CHECK(layout);

    const wgpu::PipelineLayoutDescriptor pipelineLayoutDesc //
        {
            .label = "TransformPipelineLayout",
            .bindGroupLayoutCount = 1,
            .bindGroupLayouts = &*layout,
        };

    m_TransformPipelineResources.Layout =
        gpuDevice.CreatePipelineLayout(&pipelineLayoutDesc);
    MLG_CHECK(m_TransformPipelineResources.Layout, "Failed to create transform pipeline layout");

    const wgpu::ComputePipelineDescriptor pipelineDesc//
    {
        .layout = m_TransformPipelineResources.Layout,
        .compute//
        {
            .module = m_TransformPipelineResources.Shader,
            .entryPoint = TransformShaderContract::GetEntryPoint(),
        },
    };;

    m_TransformPipeline = gpuDevice.CreateComputePipeline(&pipelineDesc);
    MLG_CHECK(m_TransformPipeline, "Failed to create compute pipeline for transform");

    return Result<>::Ok;
}

Result<>
Renderer::TransformNodes(const wgpu::Device& gpuDevice,
    const wgpu::CommandEncoder& cmdEncoder,
    const TrTransformf& cameraXForm,
    const Camera& camera,
    const Scene& scene) const
{
    // Use inverse of camera transform as view matrix
    const Mat44f viewXform = cameraXForm.Inverse().ToMatrix();
    const Mat44f& projMat = camera.GetMatrix();
    const Mat44f viewProj = projMat.Mul(viewXform);

    const ShaderInterop::CameraParams cameraParams //
        {
            .View = viewXform,
            .Projection = projMat,
            .ViewProj = viewProj,
        };

    auto cameraParamsBuf = scene.GetCameraParamsBuffer();

    gpuDevice.GetQueue().WriteBuffer(
        cameraParamsBuf.GetGpuBuffer(),
        0,
        &cameraParams,
        sizeof(ShaderInterop::CameraParams));

    const wgpu::ComputePassEncoder pass = cmdEncoder.BeginComputePass();
    pass.SetPipeline(m_TransformPipeline);
    pass.SetBindGroup(0, scene.GetTransformShaderBindGroup());
    const uint32_t workgroupCountX = narrow_cast<uint32_t>(scene.GetModelInstances().size());
    pass.DispatchWorkgroups(workgroupCountX);
    pass.End();

    return Result<>::Ok;
}