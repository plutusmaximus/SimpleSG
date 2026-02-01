#include "AppDriver.h"
#include "Application.h"
#include "FileIo.h"
#include "SdlGpuDevice.h"
#include "Stopwatch.h"
#include "scope_exit.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_gpu.h>
#include <filesystem>

AppDriver::AppDriver(AppLifecycle* appLC)
    : m_AppLifecycle(appLC)
{
}

AppDriver::~AppDriver()
{
    if(m_Window)
    {
        SDL_DestroyWindow(m_Window);
        m_Window = nullptr;
    }

    if(m_State != State::None)
    {
        SDL_Quit();
    }
}

void
AppDriver::SetMouseCapture(const bool capture)
{
    if(m_Window)
    {
        SDL_SetWindowRelativeMouseMode(m_Window, capture);
    }
}

Result<void>
AppDriver::Init()
{
    if(!everify(State::None == m_State, "AppDriver already initialized or running"))
    {
        return Error("AppDriver already initialized or running");
    }

    logSetLevel(spdlog::level::trace);

    auto cwd = std::filesystem::current_path();
    logInfo("Current working directory: {}", cwd.string());

    expect(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    SDL_Rect displayRect;
    expect(SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &displayRect), SDL_GetError());
    const int winW = displayRect.w * 3 / 4;//0.75
    const int winH = displayRect.h * 3 / 4;//0.75

    // Create window
    auto window = SDL_CreateWindow(m_AppLifecycle->GetName().data(), winW, winH, SDL_WINDOW_RESIZABLE);
    expect(window, SDL_GetError());

    m_Window = window;

    m_State = State::Initialized;

    return ResultOk;
}

Result<void>
AppDriver::Run()
{
    if(!everify(State::Initialized == m_State, "AppDriver not initialized"))
    {
        return Error("AppDriver not initialized");
    }

    m_State = State::Running;

    expect(FileIo::Startup(), "Failed to startup File I/O system");

    auto gdResult = SdlGpuDevice::Create(m_Window);
    expect(gdResult, gdResult.error());

    auto gpuDevice = *gdResult;

    ResourceCache* resourceCache = new ResourceCache(gpuDevice);
    expect(resourceCache, "Failed to create ResourceCache");

    AppContext context{ gpuDevice, resourceCache };

    Application* app = m_AppLifecycle->Create();

    auto initResult = app->Initialize(&context);
    expect(initResult, initResult.error());

    bool running = true;

    Stopwatch stopwatch;

    bool minimized = false;

    while(running && app->IsRunning())
    {
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
    }

    app->Shutdown();

    m_AppLifecycle->Destroy(app);

    delete resourceCache;

    SdlGpuDevice::Destroy(gpuDevice);

    FileIo::Shutdown();

    m_State = State::Stopped;

    return ResultOk;
}