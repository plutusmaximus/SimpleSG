#include "SdlRenderCompositor.h"

#include "PerfMetrics.h"
#include "SdlGpuDevice.h"

SdlRenderCompositor::SdlRenderCompositor(SdlGpuDevice* gpuDevice)
    : m_GpuDevice(gpuDevice)
{
}

SdlRenderCompositor::~SdlRenderCompositor()
{
    if(m_RenderFence)
    {
        auto result = WaitForFence();
        if(!result)
        {
            logError("Error waiting for render fence during SdlRenderCompositor destruction: {}", result.error());
        }
    }
}

Result<void>
SdlRenderCompositor::BeginFrame()
{
    if(!everify(!m_FrameStarted, "Frame already started"))
    {
        return Error("Frame already started");
    }

    m_FrameStarted = true;

    auto fenceResult = WaitForFence();
    expect(fenceResult, fenceResult.error());

    eassert(!m_Target, "Target should be null at the beginning of the frame");
    eassert(!m_CommandBuffer, "Command buffer should be null at the beginning of the frame");

    auto gpuDevice = m_GpuDevice->Device;

    static PerfTimer acquireCmdBufTimer("RenderCompositor.AcquireCommandBuffer");
    {
        auto scopedAcquireCmdBufTimer = acquireCmdBufTimer.StartScoped();
        m_CommandBuffer = SDL_AcquireGPUCommandBuffer(gpuDevice);
        expect(m_CommandBuffer, SDL_GetError());
    }

#if !OFFSCREEN_RENDERING
    expect(
        SDL_WaitAndAcquireGPUSwapchainTexture(m_CommandBuffer, m_GpuDevice->Window, &m_Target, nullptr, nullptr),
        SDL_GetError());

    if(!m_Target)
    {
        // No swapchain texture - likely window is minimized.
        // This is not an error, just skip rendering.
         SDL_CancelGPUCommandBuffer(m_CommandBuffer);
         m_CommandBuffer = nullptr;
         return Result<void>::Success;
    }
#endif

    return Result<void>::Success;
}

Result<void>
SdlRenderCompositor::EndFrame()
{
    if(!everify(m_FrameStarted, "Frame not started"))
    {
        return Error("Frame not started");
    }

    m_FrameStarted = false;

    if(!m_CommandBuffer)
    {
        // No command buffer - likely window is minimized. This is not an error, just skip submitting.
        return Result<void>::Success;
    }

    m_Target = nullptr;

    SDL_GPUCommandBuffer* cmdBuf = m_CommandBuffer;
    m_CommandBuffer = nullptr;

    static PerfTimer submitCmdBufferTimer("RenderCompositor.SubmitCommandBuffer");
    {
        auto scopedTimer = submitCmdBufferTimer.StartScoped();
        //expect(SDL_SubmitGPUCommandBuffer(cmdBuf), SDL_GetError());
        m_RenderFence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdBuf);
        expect(m_RenderFence, SDL_GetError());
    }

    return Result<void>::Success;
}

SDL_GPUTexture*
SdlRenderCompositor::GetTarget()
{
    eassert(m_FrameStarted, "GetTarget() called outside of a frame");

    return m_Target;
}

SDL_GPUCommandBuffer*
SdlRenderCompositor::GetCommandBuffer()
{
    eassert(m_FrameStarted, "GetCommandBuffer() called outside of a frame");

    return m_CommandBuffer;
}

Result<void>
SdlRenderCompositor::WaitForFence()
{
    if(!m_RenderFence)
    {
        return Result<void>::Success;
    }

    //Wait for the previous frame to complete
    static PerfTimer waitForFenceTimer("RenderCompositor.WaitForFence");
    {
        auto scopedWaitForFenceTimer = waitForFenceTimer.StartScoped();

        const bool success =
            SDL_WaitForGPUFences(
                m_GpuDevice->Device,
                true,
                &m_RenderFence,
                1);

        expect(success, SDL_GetError());

        SDL_ReleaseGPUFence(m_GpuDevice->Device, m_RenderFence);
        m_RenderFence = nullptr;
    }

    return Result<void>::Success;
}