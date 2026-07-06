#pragma once

#include "Result.h"

class Compositor;
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

    Result<> NewFrame(const Compositor& compositor) const;

    Result<> Composite(Compositor& compositor) const;

private:

    ImGuiContext* m_Context{nullptr};

    bool m_Initialized{ false };
};