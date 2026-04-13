#pragma once

#include "Result.h"

#include <webgpu/webgpu_cpp.h>

class DawnRenderCompositor
{
public:

    static Result<DawnRenderCompositor*> Create();
    static void Destroy(DawnRenderCompositor* compositor);

    ~DawnRenderCompositor() = default;

    Result<> BeginFrame();

    Result<> EndFrame();

    wgpu::Texture GetTarget();

    wgpu::CommandEncoder GetCommandEncoder();

private:

    DawnRenderCompositor() = default;

    wgpu::Texture m_Target{ nullptr };
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };

    bool m_FrameStarted{ false };


};