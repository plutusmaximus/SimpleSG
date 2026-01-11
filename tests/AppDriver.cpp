#include "AppDriver.h"
#include "Application.h"
#include "SDLGPUDevice.h"
#include "Stopwatch.h"
#include "scope_exit.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_gpu.h>
#include <filesystem>

AppDriver::AppDriver(Application* app)
    : m_Application(app)
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
        return std::unexpected(Error("AppDriver already initialized or running"));
    }
    
    logSetLevel(spdlog::level::trace);

    auto cwd = std::filesystem::current_path();
    logInfo("Current working directory: {}", cwd.string());

    expect(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    SDL_Rect displayRect;
    auto dm = SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &displayRect);
    const int winW = displayRect.w * 0.75;
    const int winH = displayRect.h * 0.75;

    // Create window
    auto window = SDL_CreateWindow(m_Application->GetName().data(), winW, winH, SDL_WINDOW_RESIZABLE);
    expect(window, SDL_GetError());

    m_Window = window;

    m_State = State::Initialized;

    return {};
}

Result<void>
AppDriver::Run()
{
    if(!everify(State::Initialized == m_State, "AppDriver not initialized"))
    {
        return std::unexpected(Error("AppDriver not initialized"));
    }

    m_State = State::Running;

    auto gdResult = SDLGPUDevice::Create(m_Window);
    expect(gdResult, gdResult.error());

    auto gpuDevice = *gdResult;

    ResourceCache resourceCache(gpuDevice);

    AppContext context{ gpuDevice, &resourceCache };

    auto initResult = m_Application->Initialize(&context);
    expect(initResult, initResult.error());

    bool running = true;

    Stopwatch stopwatch;

    bool minimized = false;

    while(running && m_Application->IsRunning())
    {
        m_Application->Update(stopwatch.Mark());

        SDL_Event event;

        while(minimized && running && m_Application->IsRunning() && SDL_PollEvent(&event))
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

        while(!minimized && running && m_Application->IsRunning() && SDL_PollEvent(&event))
        {
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                running = false;
                break;

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                m_Application->OnResize(event.window.data1, event.window.data2);
                break;

            case SDL_EVENT_WINDOW_MINIMIZED:
                minimized = true;
                break;

            //case SDL_EVENT_WINDOW_MOUSE_LEAVE:
            case SDL_EVENT_WINDOW_FOCUS_GAINED:
                m_Application->OnFocusGained();
                break;

            case SDL_EVENT_WINDOW_FOCUS_LOST:
                m_Application->OnFocusLost();
                break;

            case SDL_EVENT_MOUSE_MOTION:
                m_Application->OnMouseMove(Vec2f(event.motion.xrel, event.motion.yrel));
                break;

            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                m_Application->OnMouseDown(Point(event.button.x, event.button.y), event.button.button - 1);
                break;
                
            case SDL_EVENT_MOUSE_BUTTON_UP:
                m_Application->OnMouseUp(event.button.button - 1);
                break;

            case SDL_EVENT_MOUSE_WHEEL:
                m_Application->OnScroll(Vec2f(event.wheel.x, event.wheel.y));
                break;

            case SDL_EVENT_KEY_DOWN:
                m_Application->OnKeyDown(event.key.scancode);
                break;

            case SDL_EVENT_KEY_UP:
                m_Application->OnKeyUp(event.key.scancode);
                break;
            }
        }
    }

    m_Application->Shutdown();

    m_State = State::Stopped;

    return {};
}