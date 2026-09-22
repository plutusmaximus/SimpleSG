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

Shell::Shell(const char* appName, ICoopTask<System&>& appTask)
    : m_AppTask(&appTask)
    , m_SystemCreateTask(appName)
{
}

Result<> Shell::OnStart()
{
    MLG_CHECK(Stage::None == m_Stage, "Task has already been started");

    m_Stage = Stage::Stopped;

    MLG_CHECK(m_SystemCreateTask.Start(), "Failed to create System");

    m_Stage = Stage::CreatingSystem;

    return Result<>::Ok;
}

void
Shell::OnUpdate()
{
    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Shell is in None stage during update");
            break;

        case Stage::CreatingSystem:
            if(m_SystemCreateTask.IsRunning())
            {
                m_SystemCreateTask.Update();
            }
            else
            {
                auto system = m_SystemCreateTask.Take();
                if(!system)
                {
                    MLG_ERROR("Failed to create System");
                    m_Stage = Stage::Shutdown;
                }
                else
                {
                    m_System = std::move(*system);

                    if(!m_AppTask->Start(*m_System))
                    {
                        MLG_ERROR("Failed to start AppTask");
                        m_Stage = Stage::Shutdown;
                    }
                    else
                    {
                        m_Stage = Stage::Running;
                    }
                }
            }
            break;

        case Stage::Running:
        {
            MLG_SCOPED_TIMER("Frame");

            if(!m_AppTask->IsRunning())
            {
                m_Stage = Stage::Shutdown;
            }
            else if(!BeginFrame())
            {
                MLG_ERROR("Failed to begin frame");
                m_Stage = Stage::Shutdown;
            }
            else
            {
                m_AppTask->Update();

                if(!EndFrame())
                {
                    MLG_ERROR("Failed to end frame");
                    m_Stage = Stage::Shutdown;
                }
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
            SetComplete();
            break;
    }
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