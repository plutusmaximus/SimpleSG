#include "AppDriver.h"
#include "Application.h"
#include "DawnGpuDevice.h"
#include "Log.h"
#include "PerfMetrics.h"
#include "scope_exit.h"
#include "Stopwatch.h"
#include "WebgpuHelper.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_gpu.h>
#include <filesystem>
#include <thread>

AppDriver::AppDriver(AppLifecycle* appLC)
    : m_AppLifecycle(appLC)
{
}

AppDriver::~AppDriver()
{
    WebgpuHelper::Shutdown();
}

void
AppDriver::SetMouseCapture(const bool capture)
{
    SDL_SetWindowRelativeMouseMode(WebgpuHelper::GetWindow(), capture);
}

Result<>
AppDriver::Init()
{
    MLG_CHECKV(State::None == m_State, "AppDriver already initialized or running");

    Log::SetLevel(Log::Level::Trace);

    auto cwd = std::filesystem::current_path();
    MLG_INFO("Current working directory: {}", cwd.string());

    MLG_CHECK(WebgpuHelper::Startup(m_AppLifecycle->GetName().data()));

    m_State = State::Initialized;

    return Result<>::Ok;
}

Result<>
AppDriver::Run()
{
    MLG_CHECKV(State::Initialized == m_State, "AppDriver not initialized");

    m_State = State::Running;

    auto gdResult = DawnGpuDevice::Create();

    MLG_CHECK(gdResult);

    auto gpuDevice = *gdResult;

    {
        AppContext context{ gpuDevice };
        Application* app = m_AppLifecycle->Create();

        auto initResult = app->Initialize(&context);
        MLG_CHECK(initResult);

        bool running = true;

        Stopwatch stopwatch;

        bool minimized = false;

        while(running && app->IsRunning())
        {
            PerfMetrics::BeginFrame();

            app->Update(stopwatch.Mark());

            SDL_Event event;

            while(minimized && running && app->IsRunning() && SDL_PollEvent(&event))
            {
                switch(event.type)
                {
                    case SDL_EVENT_WINDOW_RESTORED:
                    case SDL_EVENT_WINDOW_MAXIMIZED:
                        minimized = false;
                        break;
                }
            }

            if(minimized)
            {
                std::this_thread::yield();
                continue;
            }

            while(!minimized && running && app->IsRunning() && SDL_PollEvent(&event))
            {
                switch (event.type)
                {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;

                case SDL_EVENT_WINDOW_RESIZED:
                case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                    app->OnResize(event.window.data1, event.window.data2);
                    break;

                case SDL_EVENT_WINDOW_MINIMIZED:
                    minimized = true;
                    break;

                //case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                    app->OnFocusGained();
                    break;

                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    app->OnFocusLost();
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    app->OnMouseMove(Vec2f(event.motion.xrel, event.motion.yrel));
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    app->OnMouseDown(Point(event.button.x, event.button.y), event.button.button - 1);
                    break;

                case SDL_EVENT_MOUSE_BUTTON_UP:
                    app->OnMouseUp(event.button.button - 1);
                    break;

                case SDL_EVENT_MOUSE_WHEEL:
                    app->OnScroll(Vec2f(event.wheel.x, event.wheel.y));
                    break;

                case SDL_EVENT_KEY_DOWN:
                    app->OnKeyDown(event.key.scancode);
                    break;

                case SDL_EVENT_KEY_UP:
                    app->OnKeyUp(event.key.scancode);
                    break;
                }
            }
    #if !defined(__EMSCRIPTEN__)
            auto dawnGpuDevice = static_cast<DawnGpuDevice*>(gpuDevice);
            MLG_CHECK(dawnGpuDevice->Surface.Present(), "Failed to present backbuffer");
    #endif

            dawnGpuDevice->Instance.ProcessEvents();

            PerfMetrics::EndFrame();
        }

        app->Shutdown();

        m_AppLifecycle->Destroy(app);
    }

    DawnGpuDevice::Destroy(gpuDevice);

    m_State = State::Stopped;

    return Result<>::Ok;
}