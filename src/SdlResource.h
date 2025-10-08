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

    const T* Get() const { return m_Resource; }

    operator T* () { return m_Resource; }

private:

    SdlResource() = delete;

    SDL_GPUDevice* m_GpuDevice = nullptr;
    T* m_Resource = nullptr;

    IMPLEMENT_REFCOUNT(SdlResource);

    IMPLEMENT_NON_COPYABLE(SdlResource);
};

struct SdlVertexBuffer : public RefPtr<SdlResource<SDL_GPUBuffer>>
{
    template<int COUNT>
    static SdlVertexBuffer Create(
        SDL_GPUDevice* gpuDevice,
        const Vertex(&vertices)[COUNT])
    {
        return Create(gpuDevice, vertices, COUNT);
    }

    static SdlVertexBuffer Create(
        SDL_GPUDevice* gpuDevice,
        const Vertex* vertices,
        const unsigned vertexCount);
};

struct SdlIndexBuffer : public RefPtr<SdlResource<SDL_GPUBuffer>>
{
    template<int COUNT>
    static SdlIndexBuffer Create(
        SDL_GPUDevice* gpuDevice,
        const uint16_t(&indices)[COUNT])
    {
        return Create(gpuDevice, indices, COUNT);
    }

    static SdlIndexBuffer Create(
        SDL_GPUDevice* gpuDevice,
        const uint16_t* indices,
        const unsigned indexCount);
};

struct SdlTexture : public RefPtr<SdlResource<SDL_GPUTexture>>
{
    static SdlTexture CreateFromPNG(SDL_GPUDevice* gpuDevice, const std::string_view path);
};