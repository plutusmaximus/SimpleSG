#pragma once

#include "CoopTask.h"
#include "Result.h"
#include "System.h"

class Shell : public ICoopTask<>
{
public:

    explicit Shell(const char* appName, ICoopTask<System&>& appTask);
    Shell() = delete;
    ~Shell() override = default;
    Shell(const Shell&) = delete;
    Shell& operator=(const Shell&) = delete;
    Shell(Shell&&) = delete;
    Shell& operator=(Shell&&) = delete;

private:
    enum class Stage
    {
        None,
        CreatingSystem,
        Running,
        Shutdown,
        Stopped
    };

    Result<> OnStart() override;

    void OnUpdate() override;

    Result<> BeginFrame();

    Result<> EndFrame();

    ICoopTask<System&>* m_AppTask{ nullptr};

    System::CreateTask m_SystemCreateTask;
    Result<System> m_System;
    
    Stage m_Stage{ Stage::None };
};

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else

void emscripten_set_main_loop(void (*func)(), int fps, int simulate_infinite_loop);

void emscripten_cancel_main_loop();

#endif // __EMSCRIPTEN__