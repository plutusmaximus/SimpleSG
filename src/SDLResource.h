#pragma once

#include "RefCount.h"

#include <string>

struct SDL_GPUDevice;

template<typename T>
class SDLResource
{
public:

    SDLResource(SDL_GPUDevice* gpuDevice, T* resource)
        : m_GpuDevice(gpuDevice)
        , m_Resource(resource)
    {
    }

    //Overloaded to release the resource.
    ~SDLResource();

    explicit operator bool() const { return m_Resource != nullptr; }

    T* Get() { return m_Resource; }

    T* Take()
    {
        T* r = m_Resource;
        m_Resource = nullptr;
        return r;
    }

private:

    SDLResource() = delete;

    SDL_GPUDevice* m_GpuDevice = nullptr;
    T* m_Resource = nullptr;

    IMPLEMENT_REFCOUNT(SDLResource);
};