#include "SDLResource.h"

#include <SDL3/SDL_gpu.h>

struct SDL_GPUBuffer;
struct SDL_GPUTexture;
struct SDL_GPUSampler;

template<>
SDLResource<SDL_GPUDevice>::~SDLResource()
{
    if (m_Resource) { SDL_DestroyGPUDevice(m_Resource); }
}

template<>
SDLResource<SDL_GPUBuffer>::~SDLResource()
{
    if (m_Resource) { SDL_ReleaseGPUBuffer(m_GpuDevice, m_Resource); }
}

template<>
SDLResource<SDL_GPUTexture>::~SDLResource()
{
    if (m_Resource) { SDL_ReleaseGPUTexture(m_GpuDevice, m_Resource); }
}

template<>
SDLResource<SDL_GPUSampler>::~SDLResource()
{
    if (m_Resource) { SDL_ReleaseGPUSampler(m_GpuDevice, m_Resource); }
}

template<>
SDLResource<SDL_GPUShader>::~SDLResource()
{
    if (m_Resource) { SDL_ReleaseGPUShader(m_GpuDevice, m_Resource); }
}