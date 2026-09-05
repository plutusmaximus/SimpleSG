#include "Shell.h"

#include "PerfMetrics.h"

#ifndef __EMSCRIPTEN__

void emscripten_set_main_loop(void (*func)(), int /*fps*/, int /*simulate_infinite_loop*/)
{
    while(EmscriptenState::IsRunning)
    {
        func();
    }
}

void emscripten_cancel_main_loop()
{
    EmscriptenState::IsRunning = false;
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
            MLG_CHECK(m_SystemCreateTask.Begin(), "Failed to create System");
            m_Stage = Stage::CreatingSystem;
        }
        break;

        case Stage::CreatingSystem:
            m_SystemCreateTask.Update();

            if(m_SystemCreateTask.IsComplete())
            {
                MLG_CHECK(m_SystemCreateTask.Succeeded(), "System creation failed");
                m_SystemInstance = m_SystemCreateTask.Take();
                MLG_CHECK(m_SystemInstance, "Failed to get System instance");

                m_Stage = Stage::Running;
            }
            break;

        case Stage::Running:
        {
            MLG_SCOPED_TIMER("Frame");

            MLG_CHECK(BeginFrame());

            const AppState appState = appUpdateCb(*m_SystemInstance);

            MLG_CHECK(EndFrame());

            if(AppState::Stopped == appState)
            {
                Shutdown();
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
    
    m_SystemInstance->ProcessEvents();

    return Result<>::Ok;
}

Result<>
Shell::EndFrame()
{
    MLG_ASSERT(Stage::Running == m_Stage, "EndFrame() called when not running");
#if !defined(__EMSCRIPTEN__)

    const GpuHelper& gpuHelper = m_SystemInstance->GetGpuHelper();

    MLG_CHECK(gpuHelper.GetSurface().Present(), "Failed to present backbuffer");
    gpuHelper.GetInstance().ProcessEvents();
#endif

    return Result<>::Ok;
}