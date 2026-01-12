#define __LOGGER_NAME__ "IMAG"

#define _CRT_SECURE_NO_WARNINGS

#include "Image.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

Result<Image>
Image::LoadFromFile(const std::string_view path)
{
    logDebug("Loading image from file: {}", path);

    // Validate input path
    expect(!path.empty(), "Image path is empty");
    expect(path.data() != nullptr, "Image path is null");

    static constexpr int DESIRED_CHANNELS = 4;

    int width, height, channels;
    unsigned char* pixels = stbi_load(path.data(), &width, &height, &channels, DESIRED_CHANNELS);

    expect(pixels, stbi_failure_reason());

    auto freePixels = [](uint8_t* p) { stbi_image_free(p); };

    // Use RefPtr constructor directly to avoid potential leak if allocation fails.
    // NOTE: With exceptions disabled (_HAS_EXCEPTIONS=0), 'new' will call std::terminate()
    // on allocation failure. The expect() check below will never trigger in practice,
    // but is kept for API consistency and defensive programming in case exception
    // handling is re-enabled in the future.
    RefPtr<SharedPixels> sharedPixels(new SharedPixels(pixels, freePixels));
    expect(sharedPixels, "Error allocating SharedPixels");

    return Image(static_cast<unsigned>(width), static_cast<unsigned>(height), sharedPixels, Flags::None);

}

Result<Image>
Image::LoadFromMemory(const std::span<const uint8_t> data)
{
    logDebug("Loading image from memory");

    // Validate input data
    expect(!data.empty(), "Image data is empty");
    expect(data.data() != nullptr, "Image data is null");

    static constexpr int DESIRED_CHANNELS = 4;

    int width, height, channels;
    unsigned char* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels, DESIRED_CHANNELS);

    expect(pixels, stbi_failure_reason());

    auto freePixels = [](uint8_t* p) { stbi_image_free(p); };

    // Use RefPtr constructor directly to avoid potential leak if allocation fails.
    // NOTE: With exceptions disabled (_HAS_EXCEPTIONS=0), 'new' will call std::terminate()
    // on allocation failure. The expect() check below will never trigger in practice,
    // but is kept for API consistency and defensive programming in case exception
    // handling is re-enabled in the future.
    RefPtr<SharedPixels> sharedPixels(new SharedPixels(pixels, freePixels));
    expect(sharedPixels, "Error allocating SharedPixels");

    return Image(static_cast<unsigned>(width), static_cast<unsigned>(height), sharedPixels, Flags::None);
}