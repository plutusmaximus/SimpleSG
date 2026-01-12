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

    [[nodiscard]] static Result<Image> LoadFromFile(const std::string_view path);

    [[nodiscard]] static Result<Image> LoadFromMemory(const std::span<const uint8_t> data);

    const unsigned Width;
    const unsigned Height;
    const Flags ImageFlags{ Flags::None };
    const std::uint8_t* const Pixels; // RGBA8

private:

    Image() = delete;

    struct SharedPixels
    {
        SharedPixels(uint8_t* pixels, void (*freePixels)(uint8_t*))
            : Pixels(pixels)
            , FreePixels(freePixels)
        {
        }

        ~SharedPixels()
        {
            if (FreePixels)
            {
                FreePixels(Pixels);
            }
        }

        uint8_t* const Pixels;
        void (*FreePixels)(uint8_t*);

        IMPLEMENT_REFCOUNT(SharedPixels);
    };

    Image(const unsigned width, const unsigned height, RefPtr<SharedPixels> pixels, const Flags flags)
        : Width(width)
        , Height(height)
        , ImageFlags(flags)
        , Pixels(pixels ? pixels->Pixels : nullptr)
        , m_SharedPixels(pixels)
    {
    }

    RefPtr<SharedPixels> m_SharedPixels;
};
