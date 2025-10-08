#pragma once

#include "Material.h"

#include "SdlResource.h"

#include "Assert.h"

#include <SDL3/SDL_gpu.h>

RefPtr<Material>
Material::Create(SDL_GPUDevice* gpuDevice, const Spec& spec)
{
    ptry
    {        // Create sampler
        SDL_GPUSamplerCreateInfo samplerInfo =
        {
            .min_filter = SDL_GPU_FILTER_NEAREST,
            .mag_filter = SDL_GPU_FILTER_NEAREST,
            .address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT,
            .address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT
        };

        RefPtr<SdlResource<SDL_GPUSampler>> sampler =
            new SdlResource<SDL_GPUSampler>(gpuDevice, SDL_CreateGPUSampler(gpuDevice, &samplerInfo));
        pcheck(sampler, "SDL_CreateGPUSampler: {}", SDL_GetError());

        auto albedo = SdlTexture::CreateFromPNG(gpuDevice, spec.Albedo);

        pcheck(albedo && *albedo, "CreateFromPNG({}) failed: {}", spec.Albedo, SDL_GetError());

        return new Material(spec.Color, albedo, sampler);
    }
    pcatchall;

    return nullptr;
}