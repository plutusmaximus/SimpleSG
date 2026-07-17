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
    : SystemCreateTask(System::Create(appName))
{
}

Result<>
Shell::BeginFrame()
{
    if(State::Running == m_State)
    {
        SystemInstance->ProcessEvents();
    }

    return Result<>::Ok;
}

Result<>
Shell::EndFrame()
{
    if(State::Running == m_State)
    {
#if !defined(__EMSCRIPTEN__)

        const GpuHelper& gpuHelper = SystemInstance->GetGpuHelper();

        MLG_CHECK(gpuHelper.GetSurface().Present(), "Failed to present backbuffer");
        gpuHelper.GetInstance().ProcessEvents();
#endif
    }
    return Result<>::Ok;
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

    switch(m_State)
    {
        case State::Init:
        {
            MLG_CHECK(SystemCreateTask, "Failed to create System");
            m_State = State::CreatingSystem;
        }
        break;

        case State::CreatingSystem:
            MLG_CHECK(SystemCreateTask->Update());

            if(SystemCreateTask->IsComplete())
            {
                MLG_CHECK(SystemCreateTask->Succeeded(), "System creation failed");
                SystemInstance = SystemCreateTask->Get();
                MLG_CHECK(SystemInstance, "Failed to get System instance");

                m_State = State::Running;
            }
            break;

        case State::Running:
        {
            MLG_SCOPED_TIMER("Frame");

            MLG_CHECK(BeginFrame());

            const AppState appState = appUpdateCb(*SystemInstance);

            MLG_CHECK(EndFrame());

            if(AppState::Stopped == appState)
            {
                Shutdown();
            }
        }
        break;

        case State::Shutdown:
            MLG_INFO("Shutting down...");
            PerfMetrics::LogCounters();
            m_State = State::Stopped;
            break;

        case State::Stopped:
            MLG_INFO("Stopped");
            break;
    }

    // We're returning successfully - cancel the shutdownOnExit.
    shutdownOnExit.release();

    return Result<>::Ok;
}