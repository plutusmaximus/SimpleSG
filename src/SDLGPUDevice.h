#pragma once

#include "GPUDevice.h"
#include "SDLResource.h"

struct SDL_GPUDevice;
struct SDL_Window;

class SDLGPUDevice : public GPUDeviceResource
{
public:

    static std::expected<GPUDevice, Error> Create(SDL_Window* window);

    ~SDLGPUDevice() override;

    std::expected<VertexBuffer, Error> CreateVertexBuffer(
        const Vertex* vertices,
        const unsigned vertexCount) override;

    std::expected<IndexBuffer, Error> CreateIndexBuffer(
        const uint16_t* indices,
        const unsigned indexCount) override;

    std::expected<Texture, Error> CreateTextureFromPNG(const std::string_view path) override;

    void* GetDevice() override { return m_GpuDevice.Get(); }  //DO NOT SUBMIT

    SDL_GPUDevice* GetSDLDevice() { return m_GpuDevice.Get(); }

private:

    SDLGPUDevice() = delete;

    SDLGPUDevice(SDL_GPUDevice* gpuDevice);

private:

    //Declare m_GpuDevice after m_Window so its destructor will be called first.
    SDLResource<SDL_GPUDevice> m_GpuDevice;
};