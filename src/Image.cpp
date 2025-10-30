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
Image::Load(const std::string_view path)
{
    static constexpr int DESIRED_CHANNELS = 4;

    int width, height, channels;
    unsigned char* pixels = stbi_load(path.data(), &width, &height, &channels, DESIRED_CHANNELS);

    expect(pixels, stbi_failure_reason());

    auto freePixels = [](uint8_t* p) { stbi_image_free(p); };

    return Create(width, height, pixels, freePixels);
}

Result<RefPtr<Image>>
Image::Create(const int width, const int height, uint8_t* pixels, void (*freePixels)(uint8_t*))
{
    Image* image = new Image(width, height, pixels, freePixels);

    expectv(pixels, "Error allocating image");

    return image;
}