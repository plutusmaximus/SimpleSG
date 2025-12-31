#pragma once

#include "Error.h"
#include "RefCount.h"

class Application;

class AppDriver
{
public:
    AppDriver(Application* app);

    ~AppDriver();

    Result<void> Run();

private:

    enum class State
    {
        Minimized,
        Normal
    };

    Application* const m_Application{ nullptr };

    State m_State{State::Normal};
};