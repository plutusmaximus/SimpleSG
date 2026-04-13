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

    wgpu::TextureView GetTarget();

    wgpu::CommandEncoder GetCommandEncoder();

private:

    DawnRenderCompositor() = default;

    wgpu::TextureView m_Target{ nullptr };
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };

    bool m_FrameStarted{ false };


};