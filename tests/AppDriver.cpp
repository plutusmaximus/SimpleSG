#include "AppDriver.h"
#include "Application.h"
#include "Finally.h"
#include "SDLGPUDevice.h"
#include "Stopwatch.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <filesystem>

AppDriver::AppDriver(Application* app)
    : m_Application(app)
{
}

AppDriver::~AppDriver()
{
}

Result<void> AppDriver::Run()
{
    Logging::SetLogLevel(spdlog::level::trace);

    auto cwd = std::filesystem::current_path();
    logInfo("Current working directory: {}", cwd.string());

    expect(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    auto sdlFinalizer = Finally([]()
    {
        SDL_Quit();
    });

    SDL_Rect displayRect;
    auto dm = SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &displayRect);
    const int winW = displayRect.w * 0.75;
    const int winH = displayRect.h * 0.75;

    // Create window
    auto window = SDL_CreateWindow(m_Application->GetName().data(), winW, winH, SDL_WINDOW_RESIZABLE);
    expect(window, SDL_GetError());

    auto windowFinalizer = Finally([window]()
    {
        SDL_DestroyWindow(window);
    });

    //DO NOT SUBMIT - make this configurable.
    /*if(!SDL_SetWindowRelativeMouseMode(window, true))
    {
        logError("Failed to set relative mouse mode: {}", SDL_GetError());
    }*/

    auto gdResult = SDLGPUDevice::Create(window);
    expect(gdResult, gdResult.error());

    auto gpuDevice = *gdResult;

    auto initResult = m_Application->Initialize(gpuDevice);
    expect(initResult, initResult.error());

    bool running = true;

    Stopwatch stopwatch;

    while(running && m_Application->IsRunning())
    {
        m_Application->Update(stopwatch.Mark());

        SDL_Event event;

        switch(m_State)
        {
            case State::Minimized:
                if(SDL_PollEvent(&event))
                {
                    switch(event.type)
                    {
                        case SDL_EVENT_WINDOW_RESTORED:
                        case SDL_EVENT_WINDOW_MAXIMIZED:
                            m_State = State::Normal;
                            break;
                    }
                }
                break;

            case State::Normal:
                if(SDL_PollEvent(&event))
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
                        m_State = State::Minimized;
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
                break;
        }
    }

    m_Application->Shutdown();

    return {};
}