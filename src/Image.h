#pragma once
#include <expected>
#include <cstdint>

#include "RefCount.h"

#include "Error.h"

class Image
{
public:

    ~Image();

    static RefPtr<Image> Create(const int width, const int height);

    const int Width;
    const int Height;
    std::uint8_t* const Pixels; // RGBA8

    static std::expected<RefPtr<Image>, Error> LoadPng(const std::string_view path);

private:

    Image(const int width, const int height, uint8_t* pixels)
        : Width(width)
        , Height(height)
        , Pixels(pixels)
    {
    }

    IMPLEMENT_REFCOUNT(Image);
};
