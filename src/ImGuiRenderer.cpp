#include "ImGuiRenderer.h"

#include "DawnGpuDevice.h"
#include "PerfMetrics.h"
#include "SdlGpuDevice.h"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlgpu3.h>
#include <imgui_impl_wgpu.h>

ImGuiRenderer::ImGuiRenderer(GpuDevice* gpuDevice)
    : m_GpuDevice(gpuDevice)
{
#if DAWN_GPU
    DawnStartup();
#else
    SdlStartup();
#endif
}

ImGuiRenderer::~ImGuiRenderer()
{
#if DAWN_GPU
    DawnShutdown();
#else
    SdlShutdown();
#endif
}

Result<void>
ImGuiRenderer::NewFrame()
{
#if DAWN_GPU
    return DawnNewFrame();
#else
    return SdlNewFrame();
#endif
}

Result<void>
ImGuiRenderer::Render(RenderCompositor* renderCompositor)
{
    static PerfTimer renderGuiTimer("ImGuiRenderer.Render");
    auto scopedTimer = renderGuiTimer.StartScoped();

#if DAWN_GPU
    return DawnRender(renderCompositor);
#else
    return SdlRender(renderCompositor);
#endif
}

//private:

Result<void>
ImGuiRenderer::DawnStartup()
{
    if(m_Context)
    {
        // Already initialized
        return Result<void>::Success;
    }

    DawnGpuDevice* dawnDevice = static_cast<DawnGpuDevice*>(m_GpuDevice);

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
    ImGui_ImplSDL3_InitForOther(dawnDevice->Window);

    ImGui_ImplWGPU_InitInfo init_info;
    init_info.Device = dawnDevice->Device.Get();
    init_info.NumFramesInFlight = 3;
    init_info.RenderTargetFormat = static_cast<WGPUTextureFormat>(dawnDevice->GetSwapChainFormat());
    init_info.DepthStencilFormat = WGPUTextureFormat_Undefined;
    ImGui_ImplWGPU_Init(&init_info);

    return Result<void>::Success;
}

Result<void>
ImGuiRenderer::DawnShutdown()
{
    if(!m_Context)
    {
        return Result<void>::Success;
    }

    ImGui_ImplWGPU_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(m_Context);

    m_Context = nullptr;

    return Result<void>::Success;
}

Result<void>
ImGuiRenderer::DawnNewFrame()
{
    ImGui_ImplWGPU_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    return Result<void>::Success;
}

Result<void>
ImGuiRenderer::DawnRender(RenderCompositor* renderCompositor)
{
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();

    if(!drawData || drawData->TotalVtxCount == 0)
    {
        // Nothing to render for ImGui
        return Result<void>::Success;
    }

    const bool is_minimized = (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f);

    if(is_minimized)
    {
        // Window is minimized, skip rendering ImGui
        return Result<void>::Success;
    }

    DawnRenderCompositor* dawnCompositor = static_cast<DawnRenderCompositor*>(renderCompositor);

    wgpu::TextureView target = dawnCompositor->GetTarget();
    wgpu::CommandEncoder cmdEncoder = dawnCompositor->GetCommandEncoder();

    if(!target)
    {
        // Off-screen rendering, skip rendering ImGui
        return Result<void>::Success;
    }

    wgpu::RenderPassColorAttachment colorAttachment //
    {
        .view = target,
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

    return Result<void>::Success;
}

Result<void>
ImGuiRenderer::SdlStartup()
{
    if(m_Context)
    {
        // Already initialized
        return Result<void>::Success;
    }

    SdlGpuDevice* sdlDevice = static_cast<SdlGpuDevice*>(m_GpuDevice);

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
    ImGui_ImplSDL3_InitForSDLGPU(sdlDevice->Window);
    ImGui_ImplSDLGPU3_InitInfo init_info = {};
    init_info.Device = sdlDevice->Device;
    init_info.ColorTargetFormat = sdlDevice->GetSwapChainFormat();
    init_info.MSAASamples = SDL_GPU_SAMPLECOUNT_1;                      // Only used in multi-viewports mode.
    init_info.SwapchainComposition = SDL_GPU_SWAPCHAINCOMPOSITION_SDR;  // Only used in multi-viewports mode.
    init_info.PresentMode = SDL_GPU_PRESENTMODE_VSYNC;
    ImGui_ImplSDLGPU3_Init(&init_info);

    return Result<void>::Success;
}

Result<void>
ImGuiRenderer::SdlShutdown()
{
    if(!m_Context)
    {
        return Result<void>::Success;
    }

    ImGui_ImplSDLGPU3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext(m_Context);

    m_Context = nullptr;

    return Result<void>::Success;
}

Result<void>
ImGuiRenderer::SdlNewFrame()
{
    ImGui_ImplSDLGPU3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    return Result<void>::Success;
}

Result<void>
ImGuiRenderer::SdlRender(RenderCompositor* renderCompositor)
{
    ImGui::Render();

    ImDrawData* drawData = ImGui::GetDrawData();

    if(!drawData || drawData->TotalVtxCount == 0)
    {
        // Nothing to render for ImGui
        return Result<void>::Success;
    }

    const bool is_minimized = (drawData->DisplaySize.x <= 0.0f || drawData->DisplaySize.y <= 0.0f);

    SdlRenderCompositor* sdlCompositor = static_cast<SdlRenderCompositor*>(renderCompositor);

    SDL_GPUTexture* target = sdlCompositor->GetTarget();
    SDL_GPUCommandBuffer* cmdBuf = sdlCompositor->GetCommandBuffer();

    if(is_minimized || !target)
    {
        // If the window is minimized, we can skip rendering the GUI without treating it as an error.
        return Result<void>::Success;
    }

    if(!target)
    {
        // Off-screen rendering, skip rendering ImGui
        return Result<void>::Success;
    }

    // This is mandatory: call ImGui_ImplSDLGPU3_PrepareDrawData() to upload the vertex/index buffer!
    ImGui_ImplSDLGPU3_PrepareDrawData(drawData, cmdBuf);

    // Setup and start a render pass
    SDL_GPUColorTargetInfo target_info//
    {
        .texture = target,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = {0, 0, 0, 0},
        .load_op = SDL_GPU_LOADOP_LOAD,
        .store_op = SDL_GPU_STOREOP_STORE,
        .cycle = false,
    };

    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(cmdBuf, &target_info, 1, nullptr);
    expect(renderPass, SDL_GetError());

    // Render ImGui
    ImGui_ImplSDLGPU3_RenderDrawData(drawData, cmdBuf, renderPass);

    SDL_EndGPURenderPass(renderPass);

    return Result<void>::Success;
}