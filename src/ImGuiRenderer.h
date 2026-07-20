#pragma once

#include "Result.h"

struct ImGuiContext;
class GpuHelper;
class GpuRenderTarget;

namespace wgpu
{
class Device;
class Texture;
} // namespace wgpu

class ImGuiRenderer
{
public:

    ImGuiRenderer() = delete;
    ~ImGuiRenderer();
    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;
    ImGuiRenderer(ImGuiRenderer&& other) noexcept
        : m_Context(other.m_Context)
    {
        other.m_Context = nullptr;
    }
    ImGuiRenderer& operator=(ImGuiRenderer&& other) noexcept
    {
        if(this != &other)
        {
            m_Context = other.m_Context;
            other.m_Context = nullptr;
        }

        return *this;
    }

    static Result<std::unique_ptr<ImGuiRenderer>> Create(GpuHelper& gpuHelper);

    template<typename Func>
    Result<> Render(const wgpu::Device& gpuDevice, const GpuRenderTarget& target, Func& renderFunc) const
    {
        static_assert(std::is_invocable_r_v<Result<>, Func>,
            "renderFunc must be callable with signature Result<> func()");

        MLG_CHECKV(m_Context, "ImGuiRenderer is not initialized");

        MLG_CHECK(NewFrame(target));

        MLG_CHECK(renderFunc());

        MLG_CHECK(Composite(gpuDevice, target));

        return Result<>::Ok;
    }

private:
    explicit ImGuiRenderer(ImGuiContext* context)
        : m_Context(context)
    {
    }

    Result<> NewFrame(const GpuRenderTarget& target) const;

    Result<> Composite(const wgpu::Device& gpuDevice, const GpuRenderTarget& target) const;

    ImGuiContext* m_Context{ nullptr };
};