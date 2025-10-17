#include "SdlResource.h"

#include <SDL3/SDL_gpu.h>

template<>
SdlResource<SDL_Window>::~SdlResource()
{
    if (m_Resource) { SDL_DestroyWindow(m_Resource); }
}

template<>
SdlResource<SDL_GPUDevice>::~SdlResource()
{
    if (m_Resource) { SDL_DestroyGPUDevice(m_Resource); }
}

template<>
SdlResource<SDL_GPUBuffer>::~SdlResource()
{
    if (m_Resource) { SDL_ReleaseGPUBuffer(m_GpuDevice, m_Resource); }
}

template<>
SdlResource<SDL_GPUTexture>::~SdlResource()
{
    if (m_Resource) { SDL_ReleaseGPUTexture(m_GpuDevice, m_Resource); }
}

template<>
SdlResource<SDL_GPUSampler>::~SdlResource()
{
    if (m_Resource) { SDL_ReleaseGPUSampler(m_GpuDevice, m_Resource); }
}

template<>
SdlResource<SDL_GPUShader>::~SdlResource()
{
    if (m_Resource) { SDL_ReleaseGPUShader(m_GpuDevice, m_Resource); }
}