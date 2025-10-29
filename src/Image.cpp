#define _CRT_SECURE_NO_WARNINGS

#include "Image.h"

#include "Error.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

Image::~Image()
{
    m_FreePixels(Pixels);
}

RefPtr<Image>
Image::Create(const int width, const int height)
{
    uint8_t* pixels = new std::uint8_t[width * height * 4];

    auto freePixels = [](uint8_t* p) { delete[] p; };

    return new Image(width, height, pixels, freePixels);
}

std::expected<RefPtr<Image>, Error>
Image::Load(const std::string_view path)
{
    static constexpr int DESIRED_CHANNELS = 4;

    int width, height, channels;
    unsigned char* data = stbi_load(path.data(), &width, &height, &channels, DESIRED_CHANNELS);

    expect(data, stbi_failure_reason());

    auto freePixels = [](uint8_t* p) { stbi_image_free(p); };

    return new Image(width, height, data, freePixels);
}