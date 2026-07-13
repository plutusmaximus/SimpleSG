#pragma once

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "ThreadPool.h"
#include <memory>

union SDL_Event;

class System
{
public:
    class CreateTask
    {
    public:
        CreateTask() = default;
        ~CreateTask() = default;
        CreateTask(const CreateTask&) = delete;
        CreateTask& operator=(const CreateTask&) = delete;
        CreateTask(CreateTask&&) = default;
        CreateTask& operator=(CreateTask&&) = default;

        bool IsValid() const;

        Result<> Update();

        bool IsComplete() const;

        bool Succeeded() const;

        /// @brief Returns the System instance if the task succeeded, otherwise returns an error.
        /// @note This method will invalidate the task, so it can only be called once.
        Result<System> Get();

    private:
        friend System;

        enum class State
        {
            None,
            CreatingGpuHelper,
            Succeeded,
            Failed
        };

        // To make the task moveable we keep it's implementation state
        // in a separate Impl struct that is heap-allocated and managed by a unique_ptr.
        struct Impl
        {
        private:

            friend System::CreateTask;

            std::optional<GpuHelper::CreateTask> m_GpuHelperTask;

            State m_State{ State::None };
        };

        Result<> Begin(const char* appName);

        std::unique_ptr<Impl> m_Impl = std::make_unique<Impl>();
    };

    System() = delete;
    ~System() = default;
    System(const System&) = delete;
    System& operator=(const System&) = delete;
    System(System&&) = default;
    System& operator=(System&&) = default;

    static Result<System> Create(const char* appName);

    GpuHelper& GetGpuHelper();

    FileFetcher& GetFileFetcher();

    ThreadPool& GetThreadPool();

    static void PostQuitEvent();

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

    bool IsMinimized() const { return m_Minimized; }

    bool ShouldQuit() const { return m_ShouldQuit; }

    bool WasFocusGained() const { return m_FocusEvent == FocusEvent::Gained; }

    bool WasFocusLost() const { return m_FocusEvent == FocusEvent::Lost; }

private:

    System(GpuHelper&& gpuHelper, FileFetcher&& fileFetcher, ThreadPool&& threadPool)
        : m_GpuHelper(std::move(gpuHelper)),
        m_FileFetcher(std::move(fileFetcher)),
        m_ThreadPool(std::move(threadPool))
    {
    }

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

    GpuHelper m_GpuHelper;
    FileFetcher m_FileFetcher;
    ThreadPool m_ThreadPool;

    FocusEvent m_FocusEvent{ FocusEvent::None };

    bool m_Minimized{ false };
    bool m_ShouldQuit{ false };
};