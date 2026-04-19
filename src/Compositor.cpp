#include "Compositor.h"

#include "PerfMetrics.h"
#include "WebgpuHelper.h"

Result<>
Compositor::Startup()
{
    MLG_CHECKV(!m_Initialized, "Compositor is already initialized");

    m_Initialized = true;

    return Result<>::Ok;
}

Result<>
Compositor::Shutdown()
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
Compositor::BeginFrame()
{
    MLG_CHECKV(m_Initialized, "Compositor is not initialized");
    MLG_CHECKV(!m_FrameStarted, "Frame already started");

    m_FrameStarted = true;

    wgpu::CommandEncoderDescriptor encoderDesc = { .label = "RenderCompositorEncoder" };

    m_CommandEncoder = WebgpuHelper::GetDevice().CreateCommandEncoder(&encoderDesc);
    MLG_CHECK(m_CommandEncoder, "Failed to create command encoder");

#if !OFFSCREEN_RENDERING
    wgpu::SurfaceTexture backbuffer;
    WebgpuHelper::GetSurface().GetCurrentTexture(&backbuffer);
    MLG_CHECK(backbuffer.texture, "Failed to get current surface texture for render pass");

    // TODO - handle SuccessSuboptimal, Timeout, Outdated, Lost, Error statuses
    MLG_CHECK(backbuffer.status == wgpu::SurfaceGetCurrentTextureStatus::SuccessOptimal,
        std::format("Backbuffer status: {}", (int)backbuffer.status));

    m_Target = backbuffer.texture;
    MLG_CHECK(m_Target, "Failed to create texture view for swapchain texture");
#endif

    return Result<>::Ok;
}

Result<>
Compositor::EndFrame()
{
    MLG_CHECKV(m_Initialized, "Compositor is not initialized");
    MLG_CHECKV(m_FrameStarted, "Frame not started");

    m_FrameStarted = false;

    m_Target = nullptr;

    wgpu::CommandEncoder cmdEncoder = m_CommandEncoder;
    m_CommandEncoder = nullptr;

    wgpu::CommandBuffer cmdBuf;
    static PerfTimer finishCmdBufferTimer("RenderCompositor.FinishCommandBuffer");
    {
        auto scopedTimer = finishCmdBufferTimer.StartScoped();
        cmdBuf = cmdEncoder.Finish(nullptr);

        MLG_CHECK(cmdBuf, "Failed to finish command buffer");
    }

    static PerfTimer submitCmdBufferTimer("RenderCompositor.SubmitCommandBuffer");
    {
        auto scopedTimer = submitCmdBufferTimer.StartScoped();
        wgpu::Queue queue = WebgpuHelper::GetDevice().GetQueue();
        MLG_CHECK(queue, "Failed to get wgpu::Queue");

        queue.Submit(1, &cmdBuf);
    }

    return Result<>::Ok;
}

wgpu::Texture
Compositor::GetTarget()
{
    MLG_ASSERT(m_Initialized, "Compositor is not initialized");
    MLG_ASSERT(m_FrameStarted, "GetTarget() called outside of a frame");

    return m_Target;

}

wgpu::CommandEncoder
Compositor::GetCommandEncoder()
{
    MLG_ASSERT(m_Initialized, "Compositor is not initialized");
    MLG_ASSERT(m_FrameStarted, "GetCommandBuffer() called outside of a frame");

    return m_CommandEncoder;
}