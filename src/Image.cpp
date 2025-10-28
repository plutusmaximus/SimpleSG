#define _CRT_SECURE_NO_WARNINGS

#include "Image.h"

#include "Error.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

Image::~Image()
{
    delete[] Pixels;
}

RefPtr<Image>
Image::Create(const int width, const int height)
{
    std::uint8_t* pixels = new std::uint8_t[width * height * 4];

    return new Image(width, height, pixels);
}

std::expected<RefPtr<Image>, Error>
Image::Load(const std::string_view path)
{
    static constexpr int DESIRED_CHANNELS = 4;

    int width, height, channels;
    unsigned char* data = stbi_load(path.data(), &width, &height, &channels, DESIRED_CHANNELS);

    expect(data, stbi_failure_reason());

    RefPtr<Image> img = Image::Create(width, height);

    std::memcpy(img->Pixels, data, width * height * DESIRED_CHANNELS);

    stbi_image_free(data);

    return img;
}