#pragma once

#include "Result.h"

struct ImGuiContext;
namespace wgpu
{
    class Texture;
}

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

    Result<> NewFrame(const wgpu::Texture& target) const;

    Result<> Composite(const wgpu::Texture& target) const;

private:

    ImGuiContext* m_Context{nullptr};

    bool m_Initialized{ false };
};