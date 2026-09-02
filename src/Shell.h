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

    /// Handles system level tasks.  Calls the application main loop handler when the system is
    /// running.
    Result<> Update(AppUpdateCallback appUpdateCb);

    bool IsRunning() const { return Stage::Running == m_Stage; }

    bool IsStopped() const { return Stage::Stopped == m_Stage; }

    void Shutdown() { m_Stage = Stage::Shutdown; }

private:
    enum class Stage
    {
        Init,
        CreatingSystem,
        Running,
        Shutdown,
        Stopped
    };

    Result<> BeginFrame();

    Result<> EndFrame();

    Result<System::CreateTask> SystemCreateTask;
    Result<System> SystemInstance;
    
    Stage m_Stage{ Stage::Init };
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