#pragma once

#include "Result.h"

#include <webgpu/webgpu_cpp.h>

class Compositor
{
public:

    Compositor() = default;
    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;
    Compositor(Compositor&&) = delete;
    Compositor& operator=(Compositor&&) = delete;

    ~Compositor()
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