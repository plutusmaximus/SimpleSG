#include "ImGuiRenderer.h"

#include "GpuHelper.h"
#include "PerfMetrics.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_wgpu.h>

Result<ImGuiRenderer>
ImGuiRenderer::Create(GpuHelper& gpuHelper)
{
    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGuiContext* context = ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard; // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;  // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;     // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Enable Multi-Viewport / Platform Windows
    // io.ConfigFlags |= ImGuiDockNodeFlags_PassthruCentralNode;

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    // ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForOther(gpuHelper.GetWindow());

    ImGui_ImplWGPU_InitInfo init_info;
    init_info.Device = gpuHelper.GetDevice().Get();
    init_info.NumFramesInFlight = 3;
    init_info.RenderTargetFormat = static_cast<WGPUTextureFormat>(gpuHelper.GetSwapChainFormat());
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&init_info);

    return ImGuiRenderer(context);
}

ImGuiRenderer::~ImGuiRenderer()
{
    if(!m_Context)
    {
        // nothing to do
        return;
        ;
    }

    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(m_Context);

    m_Context = nullptr;
}

// private:

Result<>
ImGuiRenderer::NewFrame(const ValidTexture& target) const
{
    MLG_CHECKV(m_Context, "ImGuiRenderer is not initialized");

    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();

    // ImGui assumes the target texture is the same size as the window, but this may not be true if
    // the window is resized or if the display has a different DPI scaling factor. Make sure ImGui
    // knows the current size of the target texture.

    const float width = static_cast<float>(target->GetWidth());
    const float height = static_cast<float>(target->GetHeight());

    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(width, height);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);

    ImGui::NewFrame();

    return Result<>::Ok;
}

Result<>
ImGuiRenderer::Composite(const wgpu::Device& gpuDevice, const ValidTexture& target) const
{
    MLG_CHECKV(m_Context, "ImGuiRenderer is not initialized");

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

    const wgpu::RenderPassColorAttachment colorAttachment //
        {
            .view = target->CreateView(),
            .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
            .loadOp = wgpu::LoadOp::Load,
            .storeOp = wgpu::StoreOp::Store,
            .clearValue = { .r = 0.0f, .g = 0.0f, .b = 0.0f, .a = 1.0f },
        };

    const wgpu::RenderPassDescriptor renderPassDesc //
        {
            .label = "ImGuiRenderPass",
            .colorAttachmentCount = 1,
            .colorAttachments = &colorAttachment,
        };

    const wgpu::CommandEncoderDescriptor encoderDesc = { .label = "ImGuiRenderer::Composite" };
    const wgpu::CommandEncoder cmdEncoder = gpuDevice.CreateCommandEncoder(&encoderDesc);
    MLG_CHECK(cmdEncoder, "Failed to create command encoder");

    const wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);

    ImGui_ImplWGPU_RenderDrawData(drawData, renderPass.Get());

    renderPass.End();

    const wgpu::CommandBuffer cmdBuf = cmdEncoder.Finish(nullptr);
    MLG_CHECK(cmdBuf, "Failed to finish command buffer");

    const wgpu::Queue queue = gpuDevice.GetQueue();
    MLG_CHECK(queue, "Failed to get wgpu::Queue");
    queue.Submit(1, &cmdBuf);

    return Result<>::Ok;
}
