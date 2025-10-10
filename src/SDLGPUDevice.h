#pragma once

#include "GPUDevice.h"
#include "SdlResource.h"

struct SDL_GPUDevice;
struct SDL_Window;

class SDLVertexBuffer;
class SDLIndexBuffer;

class SDLGPUDevice : public GPUDeviceResource
{
public:

    static std::expected<GPUDevice, Error> Create();

    ~SDLGPUDevice() override;

    std::expected<VertexBuffer, Error> CreateVertexBuffer(
        const Vertex* vertices,
        const unsigned vertexCount) override;

    std::expected<IndexBuffer, Error> CreateIndexBuffer(
        const uint16_t* indices,
        const unsigned indexCount) override;

    std::expected<Texture, Error> CreateTextureFromPNG(const std::string_view path) override;

    virtual void* GetDevice() { return m_GpuDevice.Get(); }  //DO NOT SUBMIT

    virtual void* GetWindow() { return m_Window.Get(); }  //DO NOT SUBMIT

private:

    SDLGPUDevice() = delete;

    SDLGPUDevice(SDL_Window* window, SDL_GPUDevice* gpuDevice)
        : m_GpuDevice(nullptr, gpuDevice)
        , m_Window(nullptr, window)
    {
    }

    SdlResource<SDL_Window> m_Window;

public:

    //Declare m_GpuDevice after m_Window so its destructor will be called first.
    SdlResource<SDL_GPUDevice> m_GpuDevice;
};