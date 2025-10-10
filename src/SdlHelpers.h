#pragma once

#include <SDL3/SDL.h>

#include "Error.h"

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

SDL_GPUShader* LoadVertexShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    const int numUniformBuffers);

SDL_GPUShader* LoadFragmentShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    const int numSamplers);