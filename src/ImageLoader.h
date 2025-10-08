#pragma once
#include <string>
#include <vector>
#include <expected>
#include <cstdint>

#include "RefCount.h"

class Image
{
public:

    static RefPtr<Image> Create(const int width, const int height)
    {
        const int size = sizeof(Image) + (width * height * 4);

        uint8_t* mem = new uint8_t[size];

        return new (mem) Image(width, height, &mem[sizeof(Image)]);
    }

    const int Width;
    const int Height;
    std::uint8_t* Pixels; // RGBA8

private:

    Image(const int width, const int height, uint8_t* pixels)
        : Width(width)
        , Height(height)
        , Pixels(pixels)
    {
    }

    IMPLEMENT_REFCOUNT(Image);
    IMPLEMENT_NON_COPYABLE(Image);
};

class ImageLoader
{
public:
    static std::expected<RefPtr<Image>, std::string> LoadPng(const std::string_view path);
};
