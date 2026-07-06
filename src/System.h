#pragma once

#include "Result.h"

union SDL_Event;

class System
{
public:
    System() = delete;

    static Result<> Startup(const char* appName);

    static void Shutdown();

    enum class EventDisposition
    {
        Ignore,
        Process
    };

    template<typename Func>
    static void ProcessEvents(const Func& eventInterceptor)
    {
        static_assert(std::is_invocable_r_v<EventDisposition, Func, const SDL_Event&>,
            "Event interceptor must be invocable with signature: EventDisposition(const SDL_Event&)");

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

    static bool IsMinimized() { return m_Minimized; }
    static bool ShouldQuit() { return m_ShouldQuit; }

    static bool WasFocusGained() { return m_FocusEvent == FocusEvent::Gained; }
    static bool WasFocusLost() { return m_FocusEvent == FocusEvent::Lost; }

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

    static void ProcessEventsImpl(const EventInterceptor& eventInterceptor);

    enum class FocusEvent
    {
        None,
        Gained,
        Lost
    };
    static inline FocusEvent m_FocusEvent{ FocusEvent::None };

    static inline bool m_Minimized{ false };
    static inline bool m_ShouldQuit{ false };
};