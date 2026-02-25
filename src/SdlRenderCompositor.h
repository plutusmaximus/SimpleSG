#pragma once

#include "RenderCompositor.h"

struct SDL_GPUCommandBuffer;
struct SDL_GPUFence;
struct SDL_GPUTexture;

class SdlGpuDevice;

class SdlRenderCompositor : public RenderCompositor
{
public:

    ~SdlRenderCompositor() override;

    Result<void> BeginFrame() override;

    Result<void> EndFrame() override;

    /// @brief Get the current render target.  Can return null if no target is available (e.g.
    /// window minimized, or when rendering offscreen).
    SDL_GPUTexture* GetTarget();

    /// @brief Get the current command buffer. Can return null if no command buffer is available (e.g.
    /// window minimized, or when rendering offscreen).
    SDL_GPUCommandBuffer* GetCommandBuffer();

private:

    friend class SdlGpuDevice;

    explicit SdlRenderCompositor(SdlGpuDevice* gpuDevice);

    Result<void> WaitForFence();

    SdlGpuDevice* m_GpuDevice{ nullptr };
    SDL_GPUTexture* m_Target{ nullptr };
    SDL_GPUCommandBuffer* m_CommandBuffer{ nullptr };
    SDL_GPUFence* m_RenderFence{ nullptr };

    bool m_FrameStarted{ false };
};