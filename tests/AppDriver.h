#pragma once

#include "Result.h"

class Application;

class AppLifecycle
{
public:

    virtual ~AppLifecycle() = default;

    virtual Application* Create() = 0;

    virtual void Destroy(Application* app) = 0;

    virtual std::string_view GetName() const = 0;
};

class AppDriver
{
public:
    explicit AppDriver(AppLifecycle* appLC);

    ~AppDriver();

    void SetMouseCapture(const bool capture);

    Result<> Init();

    Result<> Run();

private:

    enum class State
    {
        None,
        Initialized,
        Running,
        Stopped
    };

    State m_State{ State::None };

    AppLifecycle* const m_AppLifecycle{ nullptr };
};