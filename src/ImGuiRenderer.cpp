#include "ImGuiRenderer.h"

#include "Compositor.h"
#include "GpuHelper.h"
#include "PerfMetrics.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>

Result<>
ImGuiRenderer::Startup()
{
    MLG_CHECKV(!m_Initialized, "ImGuiRenderer is already initialized");

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    m_Context = ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
    //io.ConfigFlags |= ImGuiDockNodeFlags_PassthruCentralNode;


    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();
    
    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForOther(GpuHelper::GetWindow());

    ImGui_ImplWGPU_InitInfo init_info;
    init_info.Device = GpuHelper::GetDevice().Get();
    init_info.NumFramesInFlight = 3;
    init_info.RenderTargetFormat = static_cast<WGPUTextureFormat>(GpuHelper::GetSwapChainFormat());
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&init_info);

    m_Initialized = true;

    return Result<>::Ok;
}

Result<>
ImGuiRenderer::Shutdown()
{
    if(!m_Initialized)
    {
        // Not initialized, nothing to do
        return Result<>::Ok;
    }

    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(m_Context);

    m_Context = nullptr;

    m_Initialized = false;

    return Result<>::Ok;
}

Result<>
ImGuiRenderer::NewFrame(const Compositor& compositor) const
{
    MLG_CHECKV(m_Initialized, "ImGuiRenderer is not initialized");

    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();

    // ImGui assumes the target texture is the same size as the window, but this may not be true if
    // the window is resized or if the display has a different DPI scaling factor. Make sure ImGui
    // knows the current size of the target texture.

    auto texture = compositor.GetTarget();

    const float width = static_cast<float>(texture.GetWidth());
    const float height = static_cast<float>(texture.GetHeight());

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    ImGui::NewFrame();

    return Result<>::Ok;
}

Result<>
ImGuiRenderer::Composite(Compositor& compositor) const
{
    MLG_CHECKV(m_Initialized, "ImGuiRenderer is not initialized");

    MLG_SCOPED_TIMER("ImGuiRenderer.Render");

    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();

    if(!drawData || drawData->TotalVtxCount == 0)
    {
        // Nothing to render for ImGui
        return Result<>::Ok;
    }

    const bool is_minimized = (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f);

    if(is_minimized)
    {
        // Window is minimized, skip rendering ImGui
        return Result<>::Ok;
    }

    const wgpu::Texture target = compositor.GetTarget();

    if(!target)
    {
        // Off-screen rendering, skip rendering ImGui
        return Result<>::Ok;
    }

    const wgpu::RenderPassColorAttachment colorAttachment //
    {
        .view = target.CreateView(),
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = wgpu::LoadOp::Load,
        .storeOp = wgpu::StoreOp::Store,
        .clearValue = {.r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f},
    };

    const wgpu::RenderPassDescriptor renderPassDesc //
    {
        .label = "ImGuiRenderPass",
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment,
    };

    const wgpu::CommandEncoder cmdEncoder = compositor.GetCommandEncoder();

    const wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);

    ImGui_ImplWGPU_RenderDrawData(drawData, renderPass.Get());

    renderPass.End();

    return Result<>::Ok;
}
