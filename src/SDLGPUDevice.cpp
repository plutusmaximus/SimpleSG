#include "SDLGPUDevice.h"

#include "Error.h"
#include "Mesh.h"
#include "ImageLoader.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>

//#define GPU_DRIVER_DIRECT3D
#define GPU_DRIVER_VULKAN

#if defined(GPU_DRIVER_DIRECT3D)

static const SDL_GPUShaderFormat SHADER_FORMAT = SDL_GPU_SHADERFORMAT_DXIL;
static const char* const DRIVER_NAME = "direct3d12";
static const char* const SHADER_EXTENSION = ".dxil";

#elif defined(GPU_DRIVER_VULKAN)

static const SDL_GPUShaderFormat SHADER_FORMAT = SDL_GPU_SHADERFORMAT_SPIRV;
static const char* const DRIVER_NAME = "vulkan";
static const char* const SHADER_EXTENSION = ".spv";

#else

#error Must define a GPU driver to use.

#endif

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

static std::expected<SDL_GPUTexture*, std::string> CreateTexture(
    SDL_GPUDevice* gpuDevice,
    const unsigned width,
    const unsigned height,
    const uint8_t* pixels);

class SDLVertexBuffer : public VertexBufferResource
{
public:

    SDLVertexBuffer() = delete;

    SDLVertexBuffer(RefPtr<SDLGPUDevice> gpuDevice, SDL_GPUBuffer* buffer)
        : m_SDLGPUDevice(gpuDevice)
        , m_Buffer(buffer)
    {
    }

    ~SDLVertexBuffer() override
    {
        if (m_Buffer) { SDL_ReleaseGPUBuffer(m_SDLGPUDevice->m_GpuDevice.Get(), m_Buffer); }
    }

    //DO NOT SUBMIT
    void* GetBuffer() override
    {
        return m_Buffer;
    }

    RefPtr<SDLGPUDevice> m_SDLGPUDevice;

    SDL_GPUBuffer* const m_Buffer;
};

class SDLIndexBuffer : public IndexBufferResource
{
public:

    SDLIndexBuffer() = delete;

    SDLIndexBuffer(RefPtr<SDLGPUDevice> gpuDevice, SDL_GPUBuffer* buffer)
        : m_SDLGPUDevice(gpuDevice)
        , m_Buffer(buffer)
    {
    }

    ~SDLIndexBuffer() override
    {
        if (m_Buffer) { SDL_ReleaseGPUBuffer(m_SDLGPUDevice->m_GpuDevice.Get(), m_Buffer); }
    }

    //DO NOT SUBMIT
    void* GetBuffer() override
    {
        return m_Buffer;
    }

    RefPtr<SDLGPUDevice> m_SDLGPUDevice;

    SDL_GPUBuffer* const m_Buffer;
};

class SDLTexture : public TextureResource
{
public:

    SDLTexture() = delete;

    SDLTexture(RefPtr<SDLGPUDevice> gpuDevice, SDL_GPUTexture* texture)
        : m_SDLGPUDevice(gpuDevice)
        , m_Texture(texture)
    {
    }

    ~SDLTexture() override
    {
        if (m_Texture) { SDL_ReleaseGPUTexture(m_SDLGPUDevice->m_GpuDevice.Get(), m_Texture); }
    }

    //DO NOT SUBMIT
    void* GetTexture() override
    {
        return m_Texture;
    }

    RefPtr<SDLGPUDevice> m_SDLGPUDevice;

    SDL_GPUTexture* const m_Texture;
};

std::expected<GPUDevice, Error>
SDLGPUDevice::Create()
{
    logInfo("Creating SDL GPU Device...");

    expect(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

    expect(SDL_SetHint(SDL_HINT_RENDER_VULKAN_DEBUG, "1"), SDL_GetError());

    SDL_Rect displayRect;
    auto dm = SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &displayRect);
    const int winW = displayRect.w * 0.75;
    const int winH = displayRect.h * 0.75;

    // Create window
    SdlResource<SDL_Window> window{ nullptr, SDL_CreateWindow("SDL3 GPU Cube", winW, winH, SDL_WINDOW_RESIZABLE) };
    expect(window, SDL_GetError());

    // Initialize GPU device
    const bool debugMode = true;
    SdlResource<SDL_GPUDevice> gpuDevice{ nullptr, SDL_CreateGPUDevice(SHADER_FORMAT, debugMode, DRIVER_NAME) };
    expect(gpuDevice, SDL_GetError());

    expect(SDL_ClaimWindowForGPUDevice(gpuDevice.Get(), window.Get()), SDL_GetError());

    return new SDLGPUDevice(window.Take(), gpuDevice.Take());
}

SDLGPUDevice::~SDLGPUDevice()
{
}

std::expected<VertexBuffer, Error>
SDLGPUDevice::CreateVertexBuffer(const Vertex* vertices, const unsigned vertexCount)
{
    SDL_GPUBuffer* buffer = CreateGpuBuffer(m_GpuDevice.Get(), SDL_GPU_BUFFERUSAGE_VERTEX, vertices, vertexCount * sizeof(vertices[0]));
    expect(buffer, SDL_GetError());

    return new SDLVertexBuffer(this, buffer);
}

std::expected<IndexBuffer, Error>
SDLGPUDevice::CreateIndexBuffer(const uint16_t* indices, const unsigned indexCount)
{
    SDL_GPUBuffer* buffer = CreateGpuBuffer(m_GpuDevice.Get(), SDL_GPU_BUFFERUSAGE_INDEX, indices, indexCount * sizeof(indices[0]));
    expect(buffer, SDL_GetError());

    return new SDLIndexBuffer(this, buffer);
}

std::expected<Texture, Error>
SDLGPUDevice::CreateTextureFromPNG(const std::string_view path)
{
    auto imgResult = ImageLoader::LoadPng(path);
    expect(imgResult, imgResult.error());
    auto img = *imgResult;
    auto gpuTexResult = CreateTexture(m_GpuDevice.Get(), img->Width, img->Height, img->Pixels);
    expect(gpuTexResult, gpuTexResult.error());

    return new SDLTexture(this, gpuTexResult.value());
}

static SDL_GPUBuffer* CreateGpuBuffer(
    SDL_GPUDevice* gpuDevice,
    const SDL_GPUBufferUsageFlags usageFlags,
    const void* bufferData,
    const unsigned sizeofBuffer)
{
    SDL_GPUBuffer* gpuBuffer = nullptr;

    etry
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
    ecatchall;

    if (gpuBuffer)
    {
        SDL_ReleaseGPUBuffer(gpuDevice, gpuBuffer);
    }

    return nullptr;
}

static bool CopyToGpuBuffer(
    SDL_GPUDevice* gpuDevice,
    SDL_GPUBuffer* gpuBuffer,
    const void* data,
    const unsigned sizeofData)
{
    etry
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
        void* xferBuf = SDL_MapGPUTransferBuffer(gpuDevice, transferBuffer.Get(), false);

        pcheck(xferBuf, "SDL_MapGPUTransferBuffer: {}", SDL_GetError());

        ::memcpy(xferBuf, data, sizeofData);

        SDL_UnmapGPUTransferBuffer(gpuDevice, transferBuffer.Get());

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
            .transfer_buffer = transferBuffer.Get(),
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
    ecatchall;

    return false;
}

static std::expected<SDL_GPUTexture*, std::string> CreateTexture(
    SDL_GPUDevice* gpuDevice,
    const unsigned width,
    const unsigned height,
    const uint8_t* pixels)
{
    // Create GPU texture
    SDL_GPUTextureCreateInfo textureInfo =
    {
        .type = SDL_GPU_TEXTURETYPE_2D,
        .format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
        .usage = SDL_GPU_TEXTUREUSAGE_SAMPLER,
        .width = width,
        .height = height,
        .layer_count_or_depth = 1,
        .num_levels = 1,
        .sample_count = SDL_GPU_SAMPLECOUNT_1
    };

    SdlResource<SDL_GPUTexture> texture{ gpuDevice, SDL_CreateGPUTexture(gpuDevice, &textureInfo) };
    expect(texture, SDL_GetError());

    const unsigned sizeofData = width * height * 4;

    SDL_GPUTransferBufferCreateInfo xferBufferCreateInfo
    {
        .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
        .size = sizeofData
    };

    // Create transfer buffer for uploading pixel data
    SdlResource<SDL_GPUTransferBuffer> transferBuffer(gpuDevice, SDL_CreateGPUTransferBuffer(gpuDevice, &xferBufferCreateInfo));
    expect(transferBuffer, SDL_GetError());

    // Copy pixel data to transfer buffer
    void* mappedData = SDL_MapGPUTransferBuffer(gpuDevice, transferBuffer.Get(), false);
    expect(mappedData, SDL_GetError());

    ::memcpy(mappedData, pixels, sizeofData);

    SDL_UnmapGPUTransferBuffer(gpuDevice, transferBuffer.Get());

    // Upload to GPU texture
    SDL_GPUCommandBuffer* cmdBuffer = SDL_AcquireGPUCommandBuffer(gpuDevice);
    expect(cmdBuffer, SDL_GetError());

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuffer);
    if (!copyPass)
    {
        std::string error = SDL_GetError();
        SDL_CancelGPUCommandBuffer(cmdBuffer);
        expect(copyPass, error);
    }

    SDL_GPUTextureTransferInfo transferInfo
    {
        .transfer_buffer = transferBuffer.Get(),
        .offset = 0,
        .pixels_per_row = width,
        .rows_per_layer = height
    };

    SDL_GPUTextureRegion textureRegion
    {
        .texture = texture.Get(),
        .w = width,
        .h = height,
        .d = 1
    };

    SDL_UploadToGPUTexture(copyPass, &transferInfo, &textureRegion, false);

    SDL_EndGPUCopyPass(copyPass);

    expect(SDL_SubmitGPUCommandBuffer(cmdBuffer), SDL_GetError());

    return texture.Take();
}