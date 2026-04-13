#pragma once

#include "GpuDevice.h"

class Application;
struct SDL_Window;

class AppContext
{
public:

    explicit AppContext(GpuDevice* gpuDevice)
        : GpuDevice(gpuDevice)
    {
    }

    GpuDevice* const GpuDevice;
};

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