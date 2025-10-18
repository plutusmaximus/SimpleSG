#pragma once

#include "Material.h"

#include "SDLResource.h"

#include "Error.h"

#include <SDL3/SDL_gpu.h>

std::expected<RefPtr<Material>, Error>
Material::Create(GPUDevice gpuDevice, const MaterialSpec& spec)
{
    // Create sampler
    SDL_GPUSamplerCreateInfo samplerInfo =
    {
        .min_filter = SDL_GPU_FILTER_NEAREST,
        .mag_filter = SDL_GPU_FILTER_NEAREST,
        .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
        .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT
    };

    SDL_GPUDevice* gd = (SDL_GPUDevice*)gpuDevice->GetDevice();//DO NOT SUBMIT

    RefPtr<SDLResource<SDL_GPUSampler>> sampler =
        new SDLResource<SDL_GPUSampler>(gd, SDL_CreateGPUSampler(gd, &samplerInfo));
    expect(sampler, SDL_GetError());

    auto texResult = gpuDevice->CreateTextureFromPNG(spec.Albedo);

    expect(texResult, texResult.error());

    return new Material(spec.Color, texResult.value(), sampler);
}