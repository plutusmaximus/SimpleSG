#include "System.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "scope_exit.h"
#include "ThreadPool.h"

#include <filesystem>
#include <SDL3/SDL_events.h>

Result<> System::Startup(const char* appName)
{
    if(m_Initialized)
    {
        return Result<>::Ok;
    }

    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    MLG_CHECK(FileFetcher::Startup());
    MLG_DEFER_AS(fileFetcherShutdown)
    {
        FileFetcher::Shutdown();
    };

    MLG_CHECK(GpuHelper::Startup(appName));

    fileFetcherShutdown.release();

    m_Initialized = true;

    return Result<>::Ok;
}

ThreadPool&
System::GetThreadPool()
{
    static ThreadPool s_ThreadPool;

    return s_ThreadPool;
}

void
System::Shutdown()
{
    if(!m_Initialized)
    {
        return;
    }

    GpuHelper::Shutdown();
    FileFetcher::Shutdown();

    m_Initialized = false;
}

void
System::ProcessEventsImpl(const EventInterceptor& eventInterceptor)
{
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
                GpuHelper::Resize(newWidth, newHeight);
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