#include "ImGuiRenderer.h"

#include "DawnRenderCompositor.h"
#include "PerfMetrics.h"
#include "scope_exit.h"
#include "WebgpuHelper.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_impl_wgpu.h>

Result<ImGuiRenderer*>
ImGuiRenderer::Create()
{
    ImGuiRenderer* renderer = new ImGuiRenderer();
    MLG_CHECK(renderer, "Failed to create ImGuiRenderer");

    auto cleanup = scope_exit([renderer]()
    {
        ImGuiRenderer::Destroy(renderer);
    });

    MLG_CHECK(renderer->DawnStartup());

    cleanup.release();

    return renderer;
}

void
ImGuiRenderer::Destroy(ImGuiRenderer* renderer)
{
    delete renderer;
}

ImGuiRenderer::ImGuiRenderer()
{
}

ImGuiRenderer::~ImGuiRenderer()
{
    DawnShutdown();
}

Result<>
ImGuiRenderer::NewFrame()
{
    return DawnNewFrame();
}

Result<>
ImGuiRenderer::Render(DawnRenderCompositor* renderCompositor)
{
    static PerfTimer renderGuiTimer("ImGuiRenderer.Render");
    auto scopedTimer = renderGuiTimer.StartScoped();

    return DawnRender(renderCompositor);
}

//private:

Result<>
ImGuiRenderer::DawnStartup()
{
    if(m_Context)
    {
        // Already initialized
        return Result<>::Ok;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    m_Context = ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    //io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    //io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    float main_scale = SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());

    // Setup scaling
    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(main_scale);        // Bake a fixed style scale. (until we have a solution for dynamic style scaling, changing this requires resetting Style + calling this again)
    style.FontScaleDpi = main_scale;        // Set initial font scale. (in docking branch: using io.ConfigDpiScaleFonts=true automatically overrides this for every window depending on the current monitor)

    // Setup Platform/Renderer backends
    ImGui_ImplSDL3_InitForOther(WebgpuHelper::GetWindow());

    ImGui_ImplWGPU_InitInfo init_info;
    init_info.Device = WebgpuHelper::GetDevice().Get();
    init_info.NumFramesInFlight = 3;
    init_info.RenderTargetFormat = static_cast<WGPUTextureFormat>(WebgpuHelper::GetSwapChainFormat());
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&init_info);

    return Result<>::Ok;
}

Result<>
ImGuiRenderer::DawnShutdown()
{
    if(!m_Context)
    {
        return Result<>::Ok;
    }

    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(m_Context);

    m_Context = nullptr;

    return Result<>::Ok;
}

Result<>
ImGuiRenderer::DawnNewFrame()
{
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    return Result<>::Ok;
}

Result<>
ImGuiRenderer::DawnRender(DawnRenderCompositor* renderCompositor)
{
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

    wgpu::Texture target = renderCompositor->GetTarget();
    wgpu::CommandEncoder cmdEncoder = renderCompositor->GetCommandEncoder();

    if(!target)
    {
        // Off-screen rendering, skip rendering ImGui
        return Result<>::Ok;
    }

    wgpu::RenderPassColorAttachment colorAttachment //
    {
        .view = target.CreateView(),
        .depthSlice = WGPU_DEPTH_SLICE_UNDEFINED,
        .loadOp = wgpu::LoadOp::Load,
        .storeOp = wgpu::StoreOp::Store,
        .clearValue = { 0.0f, 0.0f, 0.0f, 1.0f },
    };

    wgpu::RenderPassDescriptor renderPassDesc //
    {
        .label = "ImGuiRenderPass",
        .colorAttachmentCount = 1,
        .colorAttachments = &colorAttachment,
        .depthStencilAttachment = nullptr,
    };

    wgpu::RenderPassEncoder renderPass = cmdEncoder.BeginRenderPass(&renderPassDesc);

    ImGui_ImplWGPU_RenderDrawData(drawData, renderPass.Get());

    renderPass.End();

    return Result<>::Ok;
}
