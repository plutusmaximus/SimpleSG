#include <SDL3/SDL_gpu.h>

#include <chrono>

#include "SdlHelpers.h"

#include "SdlResource.h"

static SDL_GPUShader* LoadShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    SDL_GPUShaderStage shaderStage,
    const unsigned numUniformBuffers,
    const unsigned numSamplers)
{
    void* shaderSrc = nullptr;
    SDL_GPUShader* shader = nullptr;

    ptry
    {
        size_t fileSize;
        shaderSrc = SDL_LoadFile(fileName.data(), &fileSize);
        pcheck(shaderSrc, "SDL_LoadFile({}): {}", fileName, SDL_GetError());

        SDL_GPUShaderCreateInfo shaderCreateInfo
        {
            .code_size = fileSize,
            .code = (uint8_t*)shaderSrc,
            .entrypoint = "main",
            .format = SHADER_FORMAT,
            .stage = shaderStage,
            .num_samplers = numSamplers,
            .num_uniform_buffers = numUniformBuffers
        };

        shader = SDL_CreateGPUShader(gpuDevice, &shaderCreateInfo);
        pcheck(shader, "SDL_CreateGPUShader: {}", SDL_GetError());
    }
    pcatchall;

    SDL_free(shaderSrc);

    return shader;
}

SDL_GPUShader* LoadVertexShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    const int numUniformBuffers)
{
    return LoadShader(gpuDevice, fileName, SDL_GPU_SHADERSTAGE_VERTEX, numUniformBuffers, 0);
}

SDL_GPUShader* LoadFragmentShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    const int numSamplers)
{
    return LoadShader(gpuDevice, fileName, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, numSamplers);
}

SDL_GPUTexture* CreateTexture(
    SDL_GPUDevice* gpuDevice,
    const unsigned width,
    const unsigned height,
    const uint8_t* pixels)
{
    SDL_GPUTexture* texture = nullptr;

    ptry
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

        texture = SDL_CreateGPUTexture(gpuDevice, &textureInfo);
        pcheck(texture, "SDL_CreateGPUTexture: {}", SDL_GetError());

        const unsigned sizeofData = width * height * 4;

        SDL_GPUTransferBufferCreateInfo xferBufferCreateInfo
        {
            .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
            .size = sizeofData
        };

        // Create transfer buffer for uploading pixel data
        SdlResource<SDL_GPUTransferBuffer> transferBuffer(gpuDevice, SDL_CreateGPUTransferBuffer(gpuDevice, &xferBufferCreateInfo));
        pcheck(transferBuffer, "SDL_CreateGPUTransferBuffer: {}", SDL_GetError());

        // Copy pixel data to transfer buffer
        void* mappedData = SDL_MapGPUTransferBuffer(gpuDevice, transferBuffer, false);
        pcheck(mappedData, "SDL_MapGPUTransferBuffer: {}", SDL_GetError());

        ::memcpy(mappedData, pixels, sizeofData);

        SDL_UnmapGPUTransferBuffer(gpuDevice, transferBuffer);

        // Upload to GPU texture
        SDL_GPUCommandBuffer* cmdBuffer = SDL_AcquireGPUCommandBuffer(gpuDevice);
        pcheck(cmdBuffer, "SDL_AcquireGPUCommandBuffer: {}", SDL_GetError());

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuffer);
        if (!copyPass)
        {
            std::string error = SDL_GetError();
            SDL_CancelGPUCommandBuffer(cmdBuffer);
            pcheck(copyPass, "SDL_BeginGPUCopyPass: {}", error);
        }

        SDL_GPUTextureTransferInfo transferInfo
        {
            .transfer_buffer = transferBuffer,
            .offset = 0,
            .pixels_per_row = width,
            .rows_per_layer = height
        };

        SDL_GPUTextureRegion textureRegion
        {
            .texture = texture,
            .w = width,
            .h = height,
            .d = 1
        };

        SDL_UploadToGPUTexture(copyPass, &transferInfo, &textureRegion, false);

        SDL_EndGPUCopyPass(copyPass);

        pcheck(SDL_SubmitGPUCommandBuffer(cmdBuffer), "SDL_SubmitGPUCommandBuffer: {}", SDL_GetError());

        return texture;
    }
    pcatchall;

    if (texture)
    {
        SDL_ReleaseGPUTexture(gpuDevice, texture);
    }

    return nullptr;
}
