#pragma once

#include "Error.h"
#include "RefCount.h"

class Application;
struct SDL_Window;

class AppDriver
{
public:
    explicit AppDriver(Application* app);

    ~AppDriver();

    void SetMouseCapture(const bool capture);

    Result<void> Init();

    Result<void> Run();

private:

    enum class State
    {
        None,
        Initialized,
        Running,
        Stopped
    };

    State m_State{ State::None };

    Application* const m_Application{ nullptr };
    SDL_Window* m_Window{ nullptr };
};