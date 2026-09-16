#pragma once

#include "GpuHelper.h"
#include "Result.h"

#include <memory>
#include <span>

struct ActionMapping;
union SDL_Event;
class FileFetcher;
class ImGuiRenderer;
class InputMapper;
class ThreadPool;

class System final
{
public:
    class CreateTask
    {
    public:
        explicit CreateTask(std::string appName);
        ~CreateTask();
        CreateTask(const CreateTask&) = delete;
        CreateTask& operator=(const CreateTask&) = delete;
        CreateTask(CreateTask&&) = delete;
        CreateTask& operator=(CreateTask&&) = delete;

        /// Begins the task.
        Result<> Begin();

        /// Updates the task.  This must be called periodically until IsComplete() returns
        /// true.
        void Update();

        /// Returns true if the task is running (started but not complete).
        bool IsRunning() const;

        /// Returns true if the task is complete (either succeeded or failed).
        bool IsComplete() const;

        /// Returns true if the task succeeded.
        bool Succeeded() const;

        /// Returns the System instance if the task succeeded, otherwise returns an error.
        /// @note This method will invalidate the task, so it can only be called once.
        Result<System> Take();

    private:
        friend System;

        enum class Stage
        {
            None,
            CreatingGpuHelper,
            Succeeded,
            Failed
        };

        GpuHelper::CreateTask m_GpuHelperTask;

        Stage m_Stage{ Stage::None };

        bool m_Consumed{ false };
    };

    System() = delete;
    ~System();
    System(const System&) = delete;
    System& operator=(const System&) = delete;
    System(System&&) noexcept;
    System& operator=(System&&) noexcept;

    void SetActionMapping(const std::span<const ActionMapping> actionMappings);

    GpuHelper& GetGpuHelper();
    const GpuHelper& GetGpuHelper() const;

    FileFetcher& GetFileFetcher();
    const FileFetcher& GetFileFetcher() const;

    ThreadPool& GetThreadPool();
    const ThreadPool& GetThreadPool() const;

    const ImGuiRenderer& GetImGuiRenderer() const;

    const InputMapper& GetInputMapper() const;

    static void PostQuitEvent();

    void ProcessEvents();

    /// Captures or releases the mouse cursor. When captured, the cursor is hidden and
    /// relative mouse motion events are generated. When released, the cursor is visible and
    /// absolute mouse motion events are generated.
    /// @param captured True to capture the mouse, false to release it.
    /// @return Prior capture state.
    bool SetMouseCaptured(const bool captured);

    /// Returns true if the mouse is currently captured.
    bool IsMouseCaptured() const;

    /// Returns true if the window is currently minimized.
    bool IsMinimized() const { return m_Minimized; }

    /// Returns true if the window was minimized during the last event processing.
    bool WasMinimized() const { return m_WindowStateEvent == WindowStateEvent::Minimized; }

    /// Returns true if the window was restored during the last event processing.
    bool WasRestored() const { return m_WindowStateEvent == WindowStateEvent::Restored; }

    /// Returns true if the application should quit (e.g., if a quit event was received).
    bool ShouldQuit() const { return m_ShouldQuit; }

    /// Returns true if the application gained focus during the last event processing.
    bool WasFocusGained() const { return m_FocusEvent == FocusEvent::Gained; }

    /// Returns true if the application lost focus during the last event processing.
    bool WasFocusLost() const { return m_FocusEvent == FocusEvent::Lost; }

private:
    friend CreateTask;

    System(std::unique_ptr<GpuHelper>&& gpuHelper,
        std::unique_ptr<FileFetcher>&& fileFetcher,
        std::unique_ptr<ThreadPool>&& threadPool,
        std::unique_ptr<ImGuiRenderer>&& imGuiRenderer);

    enum class FocusEvent
    {
        None,
        Gained,
        Lost
    };

    enum class WindowStateEvent
    {
        None,
        Minimized,
        Restored
    };

    std::unique_ptr<GpuHelper> m_GpuHelper;
    std::unique_ptr<FileFetcher> m_FileFetcher;
    std::unique_ptr<ThreadPool> m_ThreadPool;
    std::unique_ptr<ImGuiRenderer> m_ImGuiRenderer;

    FocusEvent m_FocusEvent{ FocusEvent::None };
    WindowStateEvent m_WindowStateEvent{ WindowStateEvent::None };

    bool m_Minimized{ false };
    bool m_ShouldQuit{ false };

    class Impl;

    std::unique_ptr<Impl> m_Impl;
};