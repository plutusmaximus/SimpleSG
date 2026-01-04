#pragma once
#include <expected>
#include <cstdint>
#include <span>

#include "RefCount.h"

#include "Error.h"

class Image
{
public:

    enum Flags : unsigned
    {
        None = 0x0,
        Translucent = 0x1,
    };

    ~Image();

    static Result<RefPtr<Image>> Create(const int width, const int height);

    static Result<RefPtr<Image>> LoadFromFile(const std::string_view path);

    static Result<RefPtr<Image>> LoadFromMemory(const std::span<const uint8_t> data);

    const unsigned Width;
    const unsigned Height;
    const Flags ImageFlags{ Flags::None };
    std::uint8_t* const Pixels; // RGBA8

private:

    static Result<RefPtr<Image>> Create(
        const unsigned width,
        const unsigned height,
        uint8_t* pixels,
        void (*freePixels)(uint8_t*));

    Image(const unsigned width, const unsigned height, uint8_t* pixels, const Flags flags, void (*freePixels)(uint8_t*))
        : Width(width)
        , Height(height)
        , ImageFlags(flags)
        , Pixels(pixels)
        , m_FreePixels(freePixels)
    {
    }

    void (*m_FreePixels)(uint8_t*);

    IMPLEMENT_REFCOUNT(Image);
};
