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

    static std::expected<RefPtr<Image>, Error> Load(const std::string_view path);

private:

    Image(const int width, const int height, uint8_t* pixels, void (*freePixels)(uint8_t*))
        : Width(width)
        , Height(height)
        , Pixels(pixels)
        , m_FreePixels(freePixels)
    {
    }

    void (*m_FreePixels)(uint8_t*);

    IMPLEMENT_REFCOUNT(Image);
};
