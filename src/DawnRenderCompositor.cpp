#include "DawnRenderCompositor.h"

#include "DawnGpuDevice.h"
#include "PerfMetrics.h"

DawnRenderCompositor::DawnRenderCompositor(DawnGpuDevice* gpuDevice)
    : m_GpuDevice(gpuDevice)
{
}

DawnRenderCompositor::~DawnRenderCompositor()
{
}

Result<void>
DawnRenderCompositor::BeginFrame()
{
    if(!everify(!m_FrameStarted, "Frame already started"))
    {
        return Error("Frame already started");
    }

    m_FrameStarted = true;

    wgpu::CommandEncoderDescriptor encoderDesc = { .label = "RenderCompositorEncoder" };

    m_CommandEncoder = m_GpuDevice->Device.CreateCommandEncoder(&encoderDesc);
    expect(m_CommandEncoder, "Failed to create command encoder");

#if !OFFSCREEN_RENDERING
    wgpu::SurfaceTexture backbuffer;
    m_GpuDevice->Surface.GetCurrentTexture(&backbuffer);
    expect(backbuffer.texture, "Failed to get current surface texture for render pass");

    // TODO - handle SuccessSuboptimal, Timeout, Outdated, Lost, Error statuses
    expect(backbuffer.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal,
        std::format("Backbuffer status: {}", (int)backbuffer.status));

    m_Target = backbuffer.texture.CreateView();
    expect(m_Target, "Failed to create texture view for swapchain texture");
#endif

    return Result<void>::Success;
}

Result<void>
DawnRenderCompositor::EndFrame()
{
    if(!everify(m_FrameStarted, "Frame not started"))
    {
        return Error("Frame not started");
    }

    m_FrameStarted = false;

    m_Target = nullptr;

    wgpu::CommandEncoder cmdEncoder = m_CommandEncoder;
    m_CommandEncoder = nullptr;

    wgpu::CommandBuffer cmdBuf;
    static PerfTimer finishCmdBufferTimer("RenderCompositor.FinishCommandBuffer");
    {
        auto scopedTimer = finishCmdBufferTimer.StartScoped();
        cmdBuf = cmdEncoder.Finish(nullptr);

        expect(cmdBuf, "Failed to finish command buffer");
    }

    static PerfTimer submitCmdBufferTimer("RenderCompositor.SubmitCommandBuffer");
    {
        auto scopedTimer = submitCmdBufferTimer.StartScoped();
        wgpu::Queue queue = m_GpuDevice->Device.GetQueue();
        expect(queue, "Failed to get wgpu::Queue");

        queue.Submit(1, &cmdBuf);
    }

    return Result<void>::Success;
}

wgpu::TextureView
DawnRenderCompositor::GetTarget()
{
    eassert(m_FrameStarted, "GetTarget() called outside of a frame");

    return m_Target;

}

wgpu::CommandEncoder
DawnRenderCompositor::GetCommandEncoder()
{
    eassert(m_FrameStarted, "GetCommandBuffer() called outside of a frame");

    return m_CommandEncoder;
}