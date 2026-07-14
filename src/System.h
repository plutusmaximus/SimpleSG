#pragma once

#include "FileFetcher.h"
#include "GpuHelper.h"
#include "ThreadPool.h"
#include <memory>

union SDL_Event;

enum class EventDisposition
{
    Ignore,
    Process
};

class EventHandler
{
public:
    EventHandler() = delete;
    ~EventHandler() = default;
    EventHandler(const EventHandler&) = delete;
    EventHandler& operator=(const EventHandler&) = delete;
    EventHandler(EventHandler&&) = delete;
    EventHandler& operator=(EventHandler&&) = delete;

    //NOLINTBEGIN
    template<typename T>
    EventHandler(EventDisposition (*func)(const SDL_Event&, T* userData), T* userData)
        : m_Invoke(&EventHandler::InvokeImpl<T>),
          m_Cb(reinterpret_cast<EventDisposition (*)(const SDL_Event&, void*)>(func)),
          m_UserData(userData)
    {
    }
    //NOLINTEND

    EventDisposition operator()(const SDL_Event& event) const
    {
        return (this->*m_Invoke)(event);
    }

private:

    // NOLINTBEGIN
    template<typename T>
    EventDisposition InvokeImpl(const SDL_Event& event) const
    {
        auto cb = reinterpret_cast<EventDisposition (*)(const SDL_Event&, T*)>(m_Cb);
        return cb(event, reinterpret_cast<T*>(m_UserData));
    }
    // NOLINTEND

    EventDisposition (EventHandler::*m_Invoke)(const SDL_Event&) const = nullptr;
    EventDisposition (*m_Cb)(const SDL_Event&, void* userData) = nullptr;
    void* m_UserData = nullptr;
};

class System final
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

    static Result<CreateTask> Create(const char* appName);

    GpuHelper& GetGpuHelper();

    FileFetcher& GetFileFetcher();

    ThreadPool& GetThreadPool();

    static void PostQuitEvent();

    void ProcessEvents(const EventHandler& eventHandler);

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