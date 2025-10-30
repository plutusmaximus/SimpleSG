#pragma once
#include <expected>
#include <cstdint>

#include "RefCount.h"

#include "Error.h"

class Image
{
public:

    ~Image();

    static Result<RefPtr<Image>> Create(const int width, const int height);

    static Result<RefPtr<Image>> Load(const std::string_view path);

    const int Width;
    const int Height;
    std::uint8_t* const Pixels; // RGBA8

private:

    static Result<RefPtr<Image>> Create(
        const int width,
        const int height,
        uint8_t* pixels,
        void (*freePixels)(uint8_t*));

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
