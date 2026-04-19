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

    ImGuiContext* m_Context{nullptr};

    bool m_Initialized{ false };
};