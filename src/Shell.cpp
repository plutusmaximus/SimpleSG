#include "Shell.h"

#include "GpuHelper.h"
#include "PerfMetrics.h"

#ifndef __EMSCRIPTEN__

namespace
{
bool& EmpscriptenIsRunning()
{
    static bool isRunning = true;
    return isRunning;
}
} // namespace

void emscripten_set_main_loop(void (*func)(), int /*fps*/, int /*simulate_infinite_loop*/)
{
    while(EmpscriptenIsRunning())
    {
        func();
    }
}

void emscripten_cancel_main_loop()
{
    EmpscriptenIsRunning() = false;
}

#endif

Shell::Shell(const char* appName)
    : m_SystemCreateTask(appName)
{
}

Result<>
Shell::Update(AppUpdateCallback appUpdateCb)
{
    // If an error occurs that results in an early exit then this
    // will run and set the state to Shutdown.
    MLG_DEFER_AS(shutdownOnExit)
    {
        Shutdown();
    };

    switch(m_Stage)
    {
        case Stage::Init:
        {
            MLG_CHECK(m_SystemCreateTask.Start(), "Failed to create System");
            m_Stage = Stage::CreatingSystem;
        }
        break;

        case Stage::CreatingSystem:
            if(m_SystemCreateTask.IsPending())
            {
                m_SystemCreateTask.Update();
            }
            else
            {
                auto system = m_SystemCreateTask.Take();
                MLG_CHECK(system, "Failed to create System");

                m_System = std::move(*system);
                m_Stage = Stage::Running;
            }
            break;

        case Stage::Running:
        {
            MLG_SCOPED_TIMER("Frame");

            MLG_CHECK(BeginFrame());

            const AppState appState = appUpdateCb(*m_System);

            if(AppState::Stopped == appState)
            {
                Shutdown();
            }
            else
            {
                MLG_CHECK(EndFrame(), "Failed to end frame");
            }
        }
        break;

        case Stage::Shutdown:
            MLG_INFO("Shutting down...");
            PerfMetrics::LogCounters();
            m_Stage = Stage::Stopped;
            break;

        case Stage::Stopped:
            MLG_INFO("Stopped");
            break;
    }

    // We're returning successfully - cancel the shutdownOnExit.
    shutdownOnExit.release();

    return Result<>::Ok;
}

// private:

Result<>
Shell::BeginFrame()
{
    MLG_ASSERT(Stage::Running == m_Stage, "BeginFrame() called when not running");
    
    m_System->ProcessEvents();

    return Result<>::Ok;
}

Result<>
Shell::EndFrame()
{
    MLG_ASSERT(Stage::Running == m_Stage, "EndFrame() called when not running");

    const GpuHelper& gpuHelper = m_System->GetGpuHelper();

    MLG_CHECK(gpuHelper.Present(), "Failed to present backbuffer");

    return Result<>::Ok;
}