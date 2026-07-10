#include "System.h"

#include <filesystem>
#include <SDL3/SDL_events.h>

Result<>
System::Startup(const char* appName)
{
    if(m_Initialized)
    {
        return Result<>::Ok;
    }

    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    auto gpuHelperResult = GpuHelper::Create(appName);
    MLG_CHECK(gpuHelperResult);

    auto fileFetcherResult = FileFetcher::Create();    
    MLG_CHECK(fileFetcherResult);

    m_GpuHelper = std::move(*gpuHelperResult);
    m_FileFetcher = std::move(*fileFetcherResult);
    m_ThreadPool = ThreadPool();

    m_Initialized = true;

    return Result<>::Ok;
}

void
System::Shutdown()
{
    if(!m_Initialized)
    {
        return;
    }

    if(m_FileFetcher)
    {
        const FileFetcher byeFileFetcher(std::move(*m_FileFetcher));
    }

    if(m_GpuHelper)
    {
        const GpuHelper byeGpuHelper(std::move(*m_GpuHelper));
    }

    if(m_ThreadPool)
    {
        const ThreadPool byeThreadPool(std::move(*m_ThreadPool));
    }

    m_Initialized = false;
}

GpuHelper&
System::GetGpuHelper()
{
    MLG_ABORTIF(!m_GpuHelper, "GpuHelper is not initialized");

    return *m_GpuHelper;
}

FileFetcher&
System::GetFileFetcher()
{
    MLG_ABORTIF(!m_FileFetcher, "FileFetcher is not initialized");

    return *m_FileFetcher;
}

ThreadPool&
System::GetThreadPool()
{
    MLG_ABORTIF(!m_ThreadPool, "ThreadPool is not initialized");

    return *m_ThreadPool;
}

void
System::ProcessEventsImpl(const EventInterceptor& eventInterceptor)
{
    MLG_ASSERT(m_Initialized, "System must be initialized before calling ProcessEvents");

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
                GetGpuHelper().Resize(newWidth, newHeight);
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