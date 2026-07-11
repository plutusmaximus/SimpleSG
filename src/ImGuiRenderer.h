#pragma once

#include "Result.h"

struct ImGuiContext;
class GpuHelper;

namespace wgpu
{
    class Device;
    class Texture;
}

class ImGuiRenderer
{
public:

    Result<> Startup(GpuHelper& gpuHelper);

    Result<> Shutdown();

    ImGuiRenderer() = default;
    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;
    ImGuiRenderer(ImGuiRenderer&&) = delete;
    ImGuiRenderer& operator=(ImGuiRenderer&&) = delete;

    ~ImGuiRenderer()
    {
        MLG_VERIFY(Shutdown());
    }

    Result<> NewFrame(const wgpu::Texture& target) const;

    Result<> Composite(const wgpu::Device& gpuDevice, const wgpu::Texture& target) const;

private:

    ImGuiContext* m_Context{nullptr};

    bool m_Initialized{ false };
};