#pragma once

#include "GpuDevice.h"
#include "ResourceCache.h"

class Application;
struct SDL_Window;

class AppContext
{
public:

    AppContext(GpuDevice* gpuDevice, ResourceCache* resourceCache)
        : GpuDevice(gpuDevice), ResourceCache(resourceCache)
    {
    }

    GpuDevice* const GpuDevice;
    ResourceCache* const ResourceCache;
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

    SDL_Window* m_Window{ nullptr };
    AppLifecycle* const m_AppLifecycle{ nullptr };
};