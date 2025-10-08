#include "SdlResource.h"

#include <SDL3/SDL_gpu.h>

#include "ImageLoader.h"
#include "SdlHelpers.h"
#include "Mesh.h"

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
SdlResource<SDL_GPUTransferBuffer>::~SdlResource()
{
    if (m_Resource) { SDL_ReleaseGPUTransferBuffer(m_GpuDevice, m_Resource); }
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

template<>
SdlResource<SDL_GPUGraphicsPipeline>::~SdlResource()
{
    if (m_Resource) { SDL_ReleaseGPUGraphicsPipeline(m_GpuDevice, m_Resource); }
}

SdlTexture SdlTexture::CreateFromPNG(SDL_GPUDevice* gpuDevice, const std::string_view path)
{
    SdlTexture texture;
    ptry
    {
        auto result = ImageLoader::LoadPng(path);
        pcheck(result, "ImageLoader::LoadPng({}) failed: {}", path.data(), result.error());
        auto img = *result;
        texture = SdlTexture{ new SdlResource<SDL_GPUTexture>(gpuDevice, CreateTexture(gpuDevice, img->Width, img->Height, img->Pixels)) };
        pcheck(*texture, "CreateTexture failed: {}", SDL_GetError());
    }
    pcatchall;

    return texture;
}

static SDL_GPUBuffer* CreateGpuBuffer(
    SDL_GPUDevice* gpuDevice,
    const SDL_GPUBufferUsageFlags usageFlags,
    const void* bufferData,
    const unsigned sizeofBuffer);

static bool CopyToGpuBuffer(
    SDL_GPUDevice* gpuDevice,
    SDL_GPUBuffer* gpuBuffer,
    const void* data,
    const unsigned sizeofData);

SdlVertexBuffer
SdlVertexBuffer::Create(
    SDL_GPUDevice* gpuDevice,
    const Vertex* vertices,
    const unsigned vertexCount)
{
    ptry
    {
        SDL_GPUBuffer * buffer = CreateGpuBuffer(gpuDevice, SDL_GPU_BUFFERUSAGE_VERTEX, vertices, vertexCount * sizeof(vertices[0]));
        pcheck(buffer, "CreateGpuBuffer failed: {}", SDL_GetError());

        return SdlVertexBuffer{ new SdlResource<SDL_GPUBuffer>(gpuDevice, buffer) };
    }
    pcatchall;

    return SdlVertexBuffer{ nullptr };
};

SdlIndexBuffer
SdlIndexBuffer::Create(
    SDL_GPUDevice* gpuDevice,
    const uint16_t* indices,
    const unsigned indexCount)
{
    ptry
    {
        SDL_GPUBuffer * buffer = CreateGpuBuffer(gpuDevice, SDL_GPU_BUFFERUSAGE_INDEX, indices, indexCount * sizeof(indices[0]));
        pcheck(buffer, "CreateGpuBuffer failed: {}", SDL_GetError());

        return SdlIndexBuffer{ new SdlResource<SDL_GPUBuffer>(gpuDevice, buffer) };
    }
    pcatchall;

    return SdlIndexBuffer{ nullptr };
}

static bool CopyToGpuBuffer(
    SDL_GPUDevice* gpuDevice,
    SDL_GPUBuffer* gpuBuffer,
    const void* data,
    const unsigned sizeofData)
{
    ptry
    {
        //Create a transfer buffer
        SDL_GPUTransferBufferCreateInfo xferBufCreateInfo
        {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = sizeofData
        };
        SdlResource<SDL_GPUTransferBuffer> transferBuffer(gpuDevice, SDL_CreateGPUTransferBuffer(gpuDevice, &xferBufCreateInfo));
        pcheck(transferBuffer, "SDL_CreateGPUTransferBuffer: {}", SDL_GetError());

        //Copy to transfer buffer
        void* xferBuf = SDL_MapGPUTransferBuffer(gpuDevice, transferBuffer, false);

        pcheck(xferBuf, "SDL_MapGPUTransferBuffer: {}", SDL_GetError());

        ::memcpy(xferBuf, data, sizeofData);

        SDL_UnmapGPUTransferBuffer(gpuDevice, transferBuffer);

        //Upload data to the buffer.

        SDL_GPUCommandBuffer* uploadCmdBuf = SDL_AcquireGPUCommandBuffer(gpuDevice);
        pcheck(uploadCmdBuf, "SDL_AcquireGPUCommandBuffer: {}", SDL_GetError());

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmdBuf);
        if (!copyPass)
        {
            std::string error = SDL_GetError();
            SDL_CancelGPUCommandBuffer(uploadCmdBuf);
            pcheck(copyPass, "SDL_AcquireGPUCommandBuffer: {}", error);

        }
        pcheck(copyPass, "SDL_BeginGPUCopyPass: {}", SDL_GetError());

        SDL_GPUTransferBufferLocation xferBufLoc
        {
            .transfer_buffer = transferBuffer,
            .offset = 0
        };
        SDL_GPUBufferRegion bufferRegion
        {
            .buffer = gpuBuffer,
            .offset = 0,
            .size = sizeofData
        };

        SDL_UploadToGPUBuffer(copyPass, &xferBufLoc, &bufferRegion, false);

        SDL_EndGPUCopyPass(copyPass);

        pcheck(SDL_SubmitGPUCommandBuffer(uploadCmdBuf), "SDL_SubmitGPUCommandBuffer: {}", SDL_GetError());

        return true;
    }
    pcatchall;

    return false;
}

static SDL_GPUBuffer* CreateGpuBuffer(
    SDL_GPUDevice* gpuDevice,
    const SDL_GPUBufferUsageFlags usageFlags,
    const void* bufferData,
    const unsigned sizeofBuffer)
{
    SDL_GPUBuffer* gpuBuffer = nullptr;

    ptry
    {
        SDL_GPUBufferCreateInfo bufferCreateInfo
        {
            .usage = usageFlags,
            .size = sizeofBuffer
        };
        gpuBuffer = SDL_CreateGPUBuffer(gpuDevice, &bufferCreateInfo);
        pcheck(gpuBuffer, "SDL_CreateGPUBuffer: {}", SDL_GetError());

        if (bufferData)
        {
            pcheck(CopyToGpuBuffer(gpuDevice, gpuBuffer, bufferData, sizeofBuffer));
        }

        return gpuBuffer;
    }
    pcatchall;

    if (gpuBuffer)
    {
        SDL_ReleaseGPUBuffer(gpuDevice, gpuBuffer);
    }

    return nullptr;
}