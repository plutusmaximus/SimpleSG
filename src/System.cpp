#include "System.h"
#include "FileFetcher.h"
#include "GpuHelper.h"

#include <filesystem>
#include <imgui_impl_sdl3.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>
#include <memory>
#include <utility>

////////// System::CreateTask

Result<>
System::CreateTask::Begin(const char* appName)
{
    MLG_CHECKV(IsValid(), "Invalid CreateTask");
    MLG_CHECKV(m_Impl->m_State == State::None, "Task is already in progress");

    MLG_INFO("Creating System...");

    auto gpuHelperTaskResult = GpuHelper::Create(appName);
    MLG_CHECK(gpuHelperTaskResult);

    m_Impl->m_GpuHelperTask = std::move(*gpuHelperTaskResult);

    m_Impl->m_State = State::CreatingGpuHelper;

    return Result<>::Ok;
}

Result<>
System::CreateTask::Update()
{
    MLG_CHECKV(IsValid(), "Invalid CreateTask");
    MLG_CHECKV(State::None != m_Impl->m_State, "Task is not started");

    switch(m_Impl->m_State)
    {
        case State::None:
            break;

        case State::CreatingGpuHelper:
            MLG_CHECKV(m_Impl->m_GpuHelperTask, "GpuHelper task is not initialized");

            if(!m_Impl->m_GpuHelperTask->IsComplete())
            {
                MLG_CHECK(m_Impl->m_GpuHelperTask->Update());
                break;
            }

            if(m_Impl->m_GpuHelperTask->Succeeded())
            {
                MLG_INFO("GpuHelper creation succeeded");
                m_Impl->m_State = State::Succeeded;
            }
            else
            {
                MLG_ERROR("GpuHelper creation failed");
                m_Impl->m_State = State::Failed;
            }
            break;

        case State::Succeeded:
        case State::Failed:
            break;
    }

    MLG_CHECK(State::Failed != m_Impl->m_State, "System creation failed");

    return Result<>::Ok;
}

bool
System::CreateTask::IsValid() const
{
    return m_Impl != nullptr;
}

bool
System::CreateTask::IsComplete() const
{
    return MLG_VERIFY(IsValid(), "Invalid CreateTask")
        && MLG_VERIFY(m_Impl->m_State != State::None, "Task is not started")
        && (State::Succeeded == m_Impl->m_State || State::Failed == m_Impl->m_State);
}

bool
System::CreateTask::Succeeded() const
{
    return MLG_VERIFY(IsValid(), "Invalid CreateTask")
        && IsComplete()
        && m_Impl->m_State == CreateTask::State::Succeeded;
}

Result<System>
System::CreateTask::Get()
{
    MLG_CHECKV(IsValid(), "Invalid CreateTask");
    MLG_CHECKV(Succeeded(), "CreateTask did not succeed");

    // Destroy on scope exit
    std::unique_ptr<Impl> bye(std::move(m_Impl));

    MLG_CHECK(bye->m_GpuHelperTask, "GpuHelper task is not initialized");
    auto gpuHelperResult = bye->m_GpuHelperTask->Get();
    MLG_CHECK(gpuHelperResult, "Failed to get GpuHelper instance");
    std::unique_ptr<GpuHelper> gpuHelper(std::move(*gpuHelperResult));
    
    auto fileFetcherResult = FileFetcher::Create();
    MLG_CHECK(fileFetcherResult, "Failed to create FileFetcher");
    std::unique_ptr<FileFetcher> fileFetcher(std::move(*fileFetcherResult));

    auto threadPoolResult = ThreadPool::Create();
    MLG_CHECK(threadPoolResult, "Failed to create ThreadPool");
    std::unique_ptr<ThreadPool> threadPool(std::move(*threadPoolResult));

    auto rendererResult = Renderer::Create(*gpuHelper, *fileFetcher);
    MLG_CHECK(rendererResult, "Failed to create Renderer");
    std::unique_ptr<Renderer> renderer(std::move(*rendererResult));

    auto imGuiRendererResult = ImGuiRenderer::Create(*gpuHelper);
    MLG_CHECK(imGuiRendererResult, "Failed to create ImGuiRenderer");
    std::unique_ptr<ImGuiRenderer> imGuiRenderer(std::move(*imGuiRendererResult));

    return System(std::move(gpuHelper),
        std::move(fileFetcher),
        std::move(threadPool),
        std::move(renderer),
        std::move(imGuiRenderer));
}

////////// System

Result<System::CreateTask>
System::Create(const char* appName)
{
    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    CreateTask createTask;

    MLG_CHECK(createTask.Begin(appName));

    return std::move(createTask);
}

GpuHelper&
System::GetGpuHelper()
{
    return *m_GpuHelper;
}

FileFetcher&
System::GetFileFetcher()
{
    return *m_FileFetcher;
}

ThreadPool&
System::GetThreadPool()
{
    return *m_ThreadPool;
}

Renderer&
System::GetRenderer()
{
    return *m_Renderer;
}

const ImGuiRenderer&
System::GetImGuiRenderer() const
{
    return *m_ImGuiRenderer;
}

void
System::PostQuitEvent()
{
    SDL_Event event;
    event.quit = SDL_QuitEvent //
        {
            .type = SDL_EVENT_QUIT,
            .timestamp = SDL_GetTicksNS(),
        };

    SDL_PushEvent(&event);
}

void
System::ProcessEvents(const EventHandler& eventHandler)
{
    m_GpuHelper->GetInstance().ProcessEvents();

    m_FocusEvent = FocusEvent::None;

    SDL_Event sdlEvent;
    while(SDL_PollEvent(&sdlEvent))
    {
        ImGui_ImplSDL3_ProcessEvent(&sdlEvent);

        switch(sdlEvent.type)
        {
            case SDL_EVENT_QUIT:
                m_ShouldQuit = true;
                break;

            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
                m_Minimized = false;
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                m_Minimized = true;
                break;

            default:
                break;
        }

        if(IsMinimized() || ShouldQuit())
        {
            continue;
        }

        if(eventHandler(sdlEvent) == EventDisposition::Ignore)
        {
            continue;
        }

        switch(sdlEvent.type)
        {
            // case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            {
                const uint32_t newWidth = static_cast<uint32_t>(sdlEvent.window.data1);
                const uint32_t newHeight = static_cast<uint32_t>(sdlEvent.window.data2);
                if(!MLG_VERIFY(GetGpuHelper().Resize(newWidth, newHeight)))
                {
                    PostQuitEvent();
                }
            }
            break;

            case SDL_EVENT_WINDOW_RESTORED:
            case SDL_EVENT_WINDOW_MAXIMIZED:
                m_Minimized = false;
                break;

            // case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                m_FocusEvent = FocusEvent::Gained;
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                m_FocusEvent = FocusEvent::Lost;
                break;

            default:
                break;
        }
    }
}