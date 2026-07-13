#include "System.h"

#include <filesystem>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_timer.h>

////////// System::CreateTaskImpl

class System::CreateTaskImpl
{
public:
    enum class State
    {
        None,
        CreatingGpuHelper,
        Succeeded,
        Failed
    };

    Result<> Begin(const char* appName);

    Result<> Update();

    bool IsComplete() const
    {
        MLG_ASSERT(State::None != m_State, "Task is not started");
        return State::Succeeded == m_State || State::Failed == m_State;
    }

private:

    friend System::CreateTask;

    std::optional<GpuHelper::CreateTask> m_GpuHelperTask;

    std::optional<System> m_System;

    State m_State{ State::None };
};

Result<>
System::CreateTaskImpl::Begin(const char* appName)
{
    MLG_ASSERT(m_State == State::None, "Task is already in progress");

    MLG_INFO("Creating System...");

    auto gpuHelperTaskResult = GpuHelper::Create(appName);
    MLG_CHECK(gpuHelperTaskResult);

    m_GpuHelperTask = std::move(*gpuHelperTaskResult);

    m_State = State::CreatingGpuHelper;

    return Result<>::Ok;
}

Result<>
System::CreateTaskImpl::Update()
{
    MLG_CHECKV(State::None != m_State, "Task is not started");

    switch(m_State)
    {
        case State::None:
            break;

        case State::CreatingGpuHelper:
            MLG_CHECKV(m_GpuHelperTask, "GpuHelper task is not initialized");

            if(!m_GpuHelperTask->IsComplete())
            {
                MLG_CHECK(m_GpuHelperTask->Update());
                break;
            }

            if(m_GpuHelperTask->Succeeded())
            {
                auto fileFetcherResult = FileFetcher::Create();    
                MLG_CHECK(fileFetcherResult);

                auto gpuHelperResult = m_GpuHelperTask->Get();
                MLG_CHECK(gpuHelperResult, "GpuHelper creation failed");

                m_System = System(std::move(*gpuHelperResult),
                    std::move(*fileFetcherResult),
                    ThreadPool());

                MLG_INFO("GpuHelper creation succeeded");

                m_State = State::Succeeded;
            }
            else
            {
                MLG_ERROR("GpuHelper creation failed");
                m_State = State::Failed;
            }
            break;

        case State::Succeeded:
        case State::Failed:
            break;
    }

    return Result<>::Ok;
}

////////// System::CreateTask

void
System::CreateTask::Deleter(CreateTaskImpl* impl)
{
    const std::unique_ptr<CreateTaskImpl> bye(impl);
}

bool
System::CreateTask::IsValid() const
{
    return m_TaskImpl != nullptr;
}

Result<>
System::CreateTask::Update()
{
    MLG_CHECKV(IsValid(), "Invalid CreateTask");

    return m_TaskImpl->Update();
}

bool
System::CreateTask::IsComplete() const
{
    return MLG_VERIFY(IsValid(), "Invalid CreateTask") && m_TaskImpl->IsComplete();
}

bool
System::CreateTask::Succeeded() const
{
    return MLG_VERIFY(IsValid(), "Invalid CreateTask")
        && m_TaskImpl->IsComplete()
        && m_TaskImpl->m_State == CreateTaskImpl::State::Succeeded;
}

Result<System>
System::CreateTask::Get()
{
    MLG_CHECKV(Succeeded(), "CreateTask did not succeed");

    UniquePtrType bye = std::move(m_TaskImpl);

    MLG_CHECKV(bye->m_System, "Invalid System");

    return std::move(*bye->m_System);
}

////////// System

Result<System>
System::Create(const char* appName)
{
    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    std::unique_ptr<CreateTaskImpl> createTaskImpl = std::make_unique<CreateTaskImpl>();

    MLG_CHECK(createTaskImpl->Begin(appName));

    CreateTask createTask(CreateTask::UniquePtrType(createTaskImpl.release(), &CreateTask::Deleter));

    while(!createTask.IsComplete())
    {
        MLG_CHECK(createTask.Update());
    }

    MLG_CHECK(createTask.Succeeded(), "System creation failed");

    return createTask.Get();

    /*auto task = GpuHelper::Create(appName);
    MLG_CHECK(task);

    while(!task->IsComplete())
    {
        MLG_CHECK(task->Update());
    }

    MLG_CHECK(task->Succeeded(), "GpuHelper creation failed");

    auto fileFetcherResult = FileFetcher::Create();    
    MLG_CHECK(fileFetcherResult);

    return System(std::move(*task->Get()), std::move(*fileFetcherResult), ThreadPool());*/
}

GpuHelper&
System::GetGpuHelper()
{
    return m_GpuHelper;
}

FileFetcher&
System::GetFileFetcher()
{
    return m_FileFetcher;
}

ThreadPool&
System::GetThreadPool()
{
    return m_ThreadPool;
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
System::ProcessEventsImpl(const EventInterceptor& eventInterceptor)
{
    m_GpuHelper.GetInstance().ProcessEvents();

    m_FocusEvent = FocusEvent::None;

    SDL_Event sdlEvent;
    while(SDL_PollEvent(&sdlEvent))
    {
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

        if(eventInterceptor(sdlEvent) == EventDisposition::Ignore)
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