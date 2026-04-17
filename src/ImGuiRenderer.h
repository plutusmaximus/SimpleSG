#pragma once

#include "Result.h"

class DawnRenderCompositor;
struct ImGuiContext;

class ImGuiRenderer
{
public:

    Result<> Startup();

    Result<> Shutdown();

    ImGuiRenderer() = default;
    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;
    ImGuiRenderer(ImGuiRenderer&&) = delete;
    ImGuiRenderer& operator=(ImGuiRenderer&&) = delete;

    ~ImGuiRenderer()
    {
        Shutdown();
    }

    Result<> NewFrame();

    Result<> Render(DawnRenderCompositor& renderCompositor);

private:

    Result<> DawnStartup();
    Result<> DawnShutdown();
    Result<> DawnNewFrame();
    Result<> DawnRender(DawnRenderCompositor& renderCompositor);

    ImGuiContext* m_Context{nullptr};

    bool m_Initialized{ false };
};