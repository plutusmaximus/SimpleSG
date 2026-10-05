#pragma once

#include "CoopTask.h"
#include "Result.h"
#include "System.h"
#include "SystemCreateTask.h"

#include <string_view>

class Shell : public ICoopTask<std::string_view>
{
public:

    explicit Shell(ICoopTask<System&>& appTask);
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

    Result<> OnStart(const std::string_view appName) override;

    void OnUpdate() override;

    Result<> BeginFrame();

    Result<> EndFrame();

    ICoopTask<System&>* m_AppTask{ nullptr};

    System::CreateTask m_SystemCreateTask;
    std::optional<System> m_OptSystem;
    System* m_System{ nullptr };
    
    Stage m_Stage{ Stage::None };
};

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else

void emscripten_set_main_loop(void (*func)(), int fps, int simulate_infinite_loop);

void emscripten_cancel_main_loop();

#endif // __EMSCRIPTEN__