#pragma once

#include "Result.h"

struct ImGuiContext;
class GpuHelper;

namespace wgpu
{
class Device;
class Texture;
} // namespace wgpu

template<typename T>
class ValidGpuObject;
using ValidTexture = ValidGpuObject<wgpu::Texture>;

class ImGuiRenderer
{
public:

    ImGuiRenderer() = delete;
    ~ImGuiRenderer();
    ImGuiRenderer(const ImGuiRenderer&) = delete;
    ImGuiRenderer& operator=(const ImGuiRenderer&) = delete;
    ImGuiRenderer(ImGuiRenderer&& other)
        : m_Context(other.m_Context)
    {
        other.m_Context = nullptr;
    }
    ImGuiRenderer& operator=(ImGuiRenderer&& other)
    {
        if(this != &other)
        {
            m_Context = other.m_Context;
            other.m_Context = nullptr;
        }

        return *this;
    }

    static Result<ImGuiRenderer> Create(GpuHelper& gpuHelper);

    Result<> NewFrame(const ValidTexture& target) const;

    Result<> Composite(const wgpu::Device& gpuDevice, const ValidTexture& target) const;

private:
    explicit ImGuiRenderer(ImGuiContext* context)
        : m_Context(context)
    {
    }

    ImGuiContext* m_Context{ nullptr };
};