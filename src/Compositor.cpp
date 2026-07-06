#include "Compositor.h"

#include "GpuHelper.h"
#include "PerfMetrics.h"

Result<>
Compositor::BeginFrame()
{
    MLG_CHECKV(!m_FrameStarted, "Frame already started");

    m_FrameStarted = true;

    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "RenderCompositorEncoder" };

    m_CommandEncoder = GpuHelper::GetDevice().CreateCommandEncoder(&encoderDesc);
    MLG_CHECK(m_CommandEncoder, "Failed to create command encoder");

#if !defined(OFFSCREEN_RENDERING) || !OFFSCREEN_RENDERING

    auto target = GpuHelper::GetSwapChainTexture();
    MLG_CHECK(target, "Failed to get swapchain texture");

    m_Target = *target;
#endif

    return Result<>::Ok;
}

Result<>
Compositor::EndFrame()
{
    MLG_CHECKV(m_FrameStarted, "Frame not started");

    m_FrameStarted = false;

    m_Target = nullptr;

    const wgpu::CommandEncoder cmdEncoder = m_CommandEncoder;
    m_CommandEncoder = nullptr;

    wgpu::CommandBuffer cmdBuf;
    {
        MLG_SCOPED_TIMER("RenderCompositor.FinishCommandBuffer");
        cmdBuf = cmdEncoder.Finish(nullptr);

        MLG_CHECK(cmdBuf, "Failed to finish command buffer");
    }

    {
        MLG_SCOPED_TIMER("RenderCompositor.SubmitCommandBuffer");
        const wgpu::Queue queue = GpuHelper::GetDevice().GetQueue();
        MLG_CHECK(queue, "Failed to get wgpu::Queue");

        queue.Submit(1, &cmdBuf);
    }

    return Result<>::Ok;
}

wgpu::Texture
Compositor::GetTarget() const
{
    MLG_ASSERT(m_FrameStarted, "GetTarget() called outside of a frame");

    return m_Target;

}

wgpu::CommandEncoder
Compositor::GetCommandEncoder() const
{
    MLG_ASSERT(m_FrameStarted, "GetCommandBuffer() called outside of a frame");

    return m_CommandEncoder;
}