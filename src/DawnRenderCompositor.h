#pragma once

#include "Result.h"

#include <webgpu/webgpu_cpp.h>

class DawnRenderCompositor
{
public:

    DawnRenderCompositor() = default;
    DawnRenderCompositor(const DawnRenderCompositor&) = delete;
    DawnRenderCompositor& operator=(const DawnRenderCompositor&) = delete;
    DawnRenderCompositor(DawnRenderCompositor&&) = delete;
    DawnRenderCompositor& operator=(DawnRenderCompositor&&) = delete;

    ~DawnRenderCompositor()
    {
        Shutdown();
    }

    Result<> Startup();

    Result<> Shutdown();

    Result<> BeginFrame();

    Result<> EndFrame();

    wgpu::Texture GetTarget();

    wgpu::CommandEncoder GetCommandEncoder();

private:

    wgpu::Texture m_Target{ nullptr };
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };

    bool m_FrameStarted{ false };

    bool m_Initialized{ false };
};