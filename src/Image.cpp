#define __LOGGER_NAME__ "IMAG"

#define _CRT_SECURE_NO_WARNINGS

#include "Image.h"
#include "scope_exit.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

Result<Image>
Image::LoadFromFile(const std::string_view path)
{
    logDebug("Loading image from file: {}", path);

    static constexpr int DESIRED_CHANNELS = 4;

    int width, height, channels;
    unsigned char* pixels = stbi_load(path.data(), &width, &height, &channels, DESIRED_CHANNELS);

    expect(pixels, stbi_failure_reason());
    
    auto cleanup = scope_exit([pixels]()
    {
        stbi_image_free(pixels);
    });

    auto freePixels = [](uint8_t* p) { stbi_image_free(p); };

    auto sharedPixels = std::make_shared<SharedPixels>(pixels, freePixels);
    expect(sharedPixels, "Error allocating SharedPixels");

    cleanup.release();

    return Image(
        static_cast<unsigned>(width),
        static_cast<unsigned>(height),
        std::move(sharedPixels),
        Image::Flags::None);

}

Result<Image>
Image::LoadFromMemory(const std::span<const uint8_t> data)
{
    logDebug("Loading image from memory");

    static constexpr int DESIRED_CHANNELS = 4;

    int width, height, channels;
    unsigned char* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels, DESIRED_CHANNELS);

    expect(pixels, stbi_failure_reason());

    auto cleanup = scope_exit([pixels]()
    {
        stbi_image_free(pixels);
    });

    auto freePixels = [](uint8_t* p) { stbi_image_free(p); };

    auto sharedPixels = std::make_shared<SharedPixels>(pixels, freePixels);
    expect(sharedPixels, "Error allocating SharedPixels");

    cleanup.release();

    return Image(static_cast<unsigned>(width), static_cast<unsigned>(height), sharedPixels, Flags::None);
}