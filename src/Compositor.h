#pragma once

#include "Result.h"

#include <webgpu/webgpu_cpp.h>

class Compositor
{
public:

    Compositor() = default;
    ~Compositor() = default;
    Compositor(const Compositor&) = delete;
    Compositor& operator=(const Compositor&) = delete;
    Compositor(Compositor&&) = delete;
    Compositor& operator=(Compositor&&) = delete;

    Result<> BeginFrame();

    Result<> EndFrame();

    wgpu::Texture GetTarget() const;

    wgpu::CommandEncoder GetCommandEncoder() const;

private:

    wgpu::Texture m_Target{ nullptr };
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };

    bool m_FrameStarted{ false };
};