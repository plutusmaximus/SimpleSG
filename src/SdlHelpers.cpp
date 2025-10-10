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