#pragma once

#include "Result.h"

struct ImGuiContext;
class GpuHelper;

namespace wgpu
{
    class Device;
    class Texture;
}

template<typename T>
class ValidGpuObject;
using ValidTexture = ValidGpuObject<wgpu::Texture>;

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

    Result<> NewFrame(const ValidTexture& target) const;

    Result<> Composite(const wgpu::Device& gpuDevice, const ValidTexture& target) const;

private:

    ImGuiContext* m_Context{nullptr};

    bool m_Initialized{ false };
};