#pragma once

#include "Result.h"

class DawnRenderCompositor;
struct ImGuiContext;

class ImGuiRenderer
{
public:

    static Result<ImGuiRenderer*> Create();
    static void Destroy(ImGuiRenderer* renderer);

    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;
    ImGuiRenderer(ImGuiRenderer&&) = delete;
    ImGuiRenderer& operator=(ImGuiRenderer&&) = delete;

    ~ImGuiRenderer();

    Result<> NewFrame();

    Result<> Render(DawnRenderCompositor& renderCompositor);

private:

    ImGuiRenderer();

    Result<> DawnStartup();
    Result<> DawnShutdown();
    Result<> DawnNewFrame();
    Result<> DawnRender(DawnRenderCompositor& renderCompositor);

    ImGuiContext* m_Context{nullptr};
};