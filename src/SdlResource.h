#pragma once

#include "RefCount.h"

#include <string>

struct SDL_GPUDevice;
struct SDL_GPUBuffer;
struct SDL_GPUTexture;
struct SDL_GPUSampler;
struct Vertex;

template<typename T>
class SdlResource
{
public:

    SdlResource(SDL_GPUDevice* gpuDevice, T* resource)
        : m_GpuDevice(gpuDevice)
        , m_Resource(resource)
    {
    }

    //Overloaded to release the resource.
    ~SdlResource();

    explicit operator bool() const { return m_Resource != nullptr; }

    T* Get() { return m_Resource; }

    T* Take()
    {
        T* r = m_Resource;
        m_Resource = nullptr;
        return r;
    }

private:

    SdlResource() = delete;

    SDL_GPUDevice* m_GpuDevice = nullptr;
    T* m_Resource = nullptr;

    IMPLEMENT_REFCOUNT(SdlResource);
};