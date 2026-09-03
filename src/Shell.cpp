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
            MLG_CHECK(SystemCreateTask, "Failed to create System");
            m_Stage = Stage::CreatingSystem;
        }
        break;

        case Stage::CreatingSystem:
            SystemCreateTask->Update();

            if(SystemCreateTask->IsComplete())
            {
                MLG_CHECK(SystemCreateTask->Succeeded(), "System creation failed");
                SystemInstance = SystemCreateTask->Take();
                MLG_CHECK(SystemInstance, "Failed to get System instance");

                m_Stage = Stage::Running;
            }
            break;

        case Stage::Running:
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
    
    SystemInstance->ProcessEvents();

    return Result<>::Ok;
}

Result<>
Shell::EndFrame()
{
    MLG_ASSERT(Stage::Running == m_Stage, "EndFrame() called when not running");
#if !defined(__EMSCRIPTEN__)

    const GpuHelper& gpuHelper = SystemInstance->GetGpuHelper();

    MLG_CHECK(gpuHelper.GetSurface().Present(), "Failed to present backbuffer");
    gpuHelper.GetInstance().ProcessEvents();
#endif

    return Result<>::Ok;
}