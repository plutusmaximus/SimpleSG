#pragma once

#include "GPUDevice.h"
#include "ResourceCache.h"

class Application;
struct SDL_Window;

class AppContext
{
public:

    AppContext(GPUDevice gpuDevice, ResourceCache* resourceCache)
        : m_GpuDevice(gpuDevice), m_ResourceCache(resourceCache)
    {
    }

    GPUDevice GetGpuDevice(){ return m_GpuDevice; }

    ResourceCache* GetResourceCache(){ return m_ResourceCache; }

private:
    GPUDevice m_GpuDevice;
    ResourceCache* m_ResourceCache;
};

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