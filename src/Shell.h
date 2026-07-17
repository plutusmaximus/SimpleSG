#pragma once

#include "Result.h"
#include "System.h"

class Shell
{
public:
    enum class AppState
    {
        Running,
        Stopped
    };

    using AppUpdateCallback = AppState (*)(System& system);

    explicit Shell(const char* appName);

    Result<> BeginFrame();

    Result<> EndFrame();

    /// Handles system level tasks.  Calls the application main loop handler when the system is
    /// running.
    Result<> Update(AppUpdateCallback appUpdateCb);

    bool IsRunning() const { return State::Running == m_State; }

    bool IsStopped() const { return State::Stopped == m_State; }

    void Shutdown() { m_State = State::Shutdown; }

private:
    enum class State
    {
        Init,
        CreatingSystem,
        Running,
        Shutdown,
        Stopped
    };

    Result<System::CreateTask> SystemCreateTask;
    Result<System> SystemInstance;
    
    State m_State{ State::Init };
};

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else

struct EmscriptenState
{
    static inline bool IsRunning{ true };
};

void emscripten_set_main_loop(void (*func)(), int fps, int simulate_infinite_loop);

void emscripten_cancel_main_loop();

#endif // __EMSCRIPTEN__