#pragma once

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "ThreadPool.h"

#include <optional>

union SDL_Event;

class System
{
public:
    System() = default;

    Result<> Startup(const char* appName);

    void Shutdown();

    GpuHelper& GetGpuHelper();

    FileFetcher& GetFileFetcher();

    ThreadPool& GetThreadPool();

    enum class EventDisposition
    {
        Ignore,
        Process
    };

    template<typename Func>
    void ProcessEvents(const Func& eventInterceptor)
    {
        static_assert(std::is_invocable_r_v<EventDisposition, Func, const SDL_Event&>,
            "Event interceptor must be invocable with signature: EventDisposition(const SDL_Event&)");

        if(!MLG_VERIFY(m_Initialized, "System::Startup() has not been called"))
        {
            return;
        }

        struct Interceptor : public EventInterceptor
        {
            explicit Interceptor(const Func& func)
                : m_Func(func)
            {
            }

            EventDisposition operator()(const SDL_Event& event) const override
            {
                return std::invoke(m_Func, event);
            }

            const Func& m_Func;
        };

        ProcessEventsImpl(Interceptor(eventInterceptor));
    }

    bool IsMinimized() const
    {
        MLG_ASSERT(m_Initialized);
        return m_Minimized;
    }

    bool ShouldQuit() const
    {
        MLG_ASSERT(m_Initialized);
        return m_ShouldQuit;
    }

    bool WasFocusGained() const
    {
        MLG_ASSERT(m_Initialized);
        return m_FocusEvent == FocusEvent::Gained;
    }
    
    bool WasFocusLost() const
    {
        MLG_ASSERT(m_Initialized);
        return m_FocusEvent == FocusEvent::Lost;
    }

private:

    struct EventInterceptor
    {
        EventInterceptor() = default;
        virtual ~EventInterceptor() = default;
        EventInterceptor(const EventInterceptor&) = delete;
        EventInterceptor& operator=(const EventInterceptor&) = delete;
        EventInterceptor(EventInterceptor&&) = delete;
        EventInterceptor& operator=(EventInterceptor&&) = delete;

        virtual EventDisposition operator()(const SDL_Event& event) const = 0;
    };

    void ProcessEventsImpl(const EventInterceptor& eventInterceptor);

    enum class FocusEvent
    {
        None,
        Gained,
        Lost
    };

    std::optional<GpuHelper> m_GpuHelper;
    std::optional<FileFetcher> m_FileFetcher;
    std::optional<ThreadPool> m_ThreadPool;

    FocusEvent m_FocusEvent{ FocusEvent::None };

    bool m_Minimized{ false };
    bool m_ShouldQuit{ false };

    bool m_Initialized{ false };
};