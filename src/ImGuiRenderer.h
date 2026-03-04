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

    Result<> NewFrame();

    Result<> Render(RenderCompositor* renderCompositor);

private:

    Result<> DawnStartup();
    Result<> DawnShutdown();
    Result<> DawnNewFrame();
    Result<> DawnRender(RenderCompositor* renderCompositor);

    Result<> SdlStartup();
    Result<> SdlShutdown();
    Result<> SdlNewFrame();
    Result<> SdlRender(RenderCompositor* renderCompositor);

    GpuDevice* m_GpuDevice;

    ImGuiContext* m_Context{nullptr};
};