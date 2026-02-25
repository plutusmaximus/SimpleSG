#pragma once

#include "RenderCompositor.h"

#include <webgpu/webgpu_cpp.h>

class DawnGpuDevice;

class DawnRenderCompositor : public RenderCompositor
{
public:

    ~DawnRenderCompositor() override;

    Result<void> BeginFrame() override;

    Result<void> EndFrame() override;

    wgpu::TextureView GetTarget();

    wgpu::CommandEncoder GetCommandEncoder();

private:

    friend class DawnGpuDevice;

    explicit DawnRenderCompositor(DawnGpuDevice* gpuDevice);

    DawnGpuDevice* m_GpuDevice{ nullptr };
    wgpu::TextureView m_Target{ nullptr };
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };

    bool m_FrameStarted{ false };


};