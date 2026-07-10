#include "System.h"

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "ThreadPool.h"

#include <filesystem>
#include <SDL3/SDL_events.h>
#include <memory>

namespace
{
std::unique_ptr<GpuHelper> s_GpuHelper; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::unique_ptr<FileFetcher> s_FileFetcher; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::unique_ptr<ThreadPool> s_ThreadPool; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
} // namespace

Result<> System::Startup(const char* appName)
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

    s_GpuHelper = std::make_unique<GpuHelper>(std::move(*gpuHelperResult));
    s_FileFetcher = std::make_unique<FileFetcher>(std::move(*fileFetcherResult));
    s_ThreadPool = std::make_unique<ThreadPool>();

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

    const FileFetcher byeFileFetcher(std::move(*s_FileFetcher));
    const GpuHelper byeGpuHelper(std::move(*s_GpuHelper));
    const ThreadPool byeThreadPool(std::move(*s_ThreadPool));

    m_Initialized = false;
}

GpuHelper&
System::GetGpuHelper()
{
    MLG_ASSERT(m_Initialized, "System must be initialized before calling GetGpuHelper");

    return *s_GpuHelper;
}

FileFetcher&
System::GetFileFetcher()
{
    MLG_ASSERT(m_Initialized, "System must be initialized before calling GetFileFetcher");

    return *s_FileFetcher;
}

ThreadPool&
System::GetThreadPool()
{
    MLG_ASSERT(m_Initialized, "System must be initialized before calling GetThreadPool");

    return *s_ThreadPool;
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