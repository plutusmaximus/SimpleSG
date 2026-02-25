#pragma once

#include "Result.h"

class GpuDevice;
class RenderCompositor;
struct ImGuiContext;

class ImGuiRenderer
{
public:

    explicit ImGuiRenderer(GpuDevice* gpuDevice);

    ~ImGuiRenderer();

    Result<void> NewFrame();

    Result<void> Render(RenderCompositor* renderCompositor);

private:

    Result<void> DawnStartup();
    Result<void> DawnShutdown();
    Result<void> DawnNewFrame();
    Result<void> DawnRender(RenderCompositor* renderCompositor);

    Result<void> SdlStartup();
    Result<void> SdlShutdown();
    Result<void> SdlNewFrame();
    Result<void> SdlRender(RenderCompositor* renderCompositor);

    GpuDevice* m_GpuDevice;

    ImGuiContext* m_Context{nullptr};
};