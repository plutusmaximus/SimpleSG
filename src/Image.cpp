#define _CRT_SECURE_NO_WARNINGS

#include "Image.h"

#include "Error.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

Image::~Image()
{
    m_FreePixels(Pixels);
}

Result<RefPtr<Image>>
Image::Create(const int width, const int height)
{
    uint8_t* pixels = new uint8_t[width * height * 4];

    expectv(pixels, "Error allocating pixels");

    auto freePixels = [](uint8_t* p) { delete[] p; };

    return Create(width, height, pixels, freePixels);
}

Result<RefPtr<Image>>
Image::LoadFromFile(const std::string_view path)
{
    logDebug("Loading image from file: {}", path);

    static constexpr int DESIRED_CHANNELS = 4;

    int width, height, channels;
    unsigned char* pixels = stbi_load(path.data(), &width, &height, &channels, DESIRED_CHANNELS);

    expect(pixels, stbi_failure_reason());

    auto freePixels = [](uint8_t* p) { stbi_image_free(p); };

    return Create(width, height, pixels, freePixels);
}

Result<RefPtr<Image>>
Image::LoadFromMemory(const std::span<const uint8_t> data)
{
    logDebug("Loading image from memory");

    static constexpr int DESIRED_CHANNELS = 4;

    int width, height, channels;
    unsigned char* pixels = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &width, &height, &channels, DESIRED_CHANNELS);

    expect(pixels, stbi_failure_reason());

    auto freePixels = [](uint8_t* p) { stbi_image_free(p); };

    return Create(width, height, pixels, freePixels);
}

//private:

Result<RefPtr<Image>>
Image::Create(const int width, const int height, uint8_t* pixels, void (*freePixels)(uint8_t*))
{
    const size_t imageSize = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
    Flags flags = Flags::None;
    for(size_t i = 3; i < imageSize; i += 4)
    {
        if(pixels[i] != 255)
        {
            flags = Flags::Translucent;
            break;
        }
    }
    
    Image* image = new Image(width, height, pixels, flags, freePixels);

    expectv(pixels, "Error allocating image");

    return image;
}