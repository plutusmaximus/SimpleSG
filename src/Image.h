#pragma once
#include <expected>
#include <cstdint>
#include <span>
#include <memory>

#include "Error.h"

class Image
{
public:

    enum Flags : unsigned
    {
        None = 0x0,
        Translucent = 0x1,
    };

    Image(const Image& other)
        : Width(other.Width)
        , Height(other.Height)
        , ImageFlags(other.ImageFlags)
        , Pixels(other.Pixels)
        , m_SharedPixels(other.m_SharedPixels)
    {
    }

    Image(Image&& other) noexcept
        : Width(other.Width)
        , Height(other.Height)
        , ImageFlags(other.ImageFlags)
        , Pixels(other.Pixels)
        , m_SharedPixels(std::move(other.m_SharedPixels))
    {
    }

    static Result<Image> LoadFromFile(const std::string_view path);

    static Result<Image> LoadFromMemory(const std::span<const uint8_t> data);

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
    };

    Image(const unsigned width, const unsigned height, const std::shared_ptr<SharedPixels>& pixels, const Flags flags)
        : Width(width)
        , Height(height)
        , ImageFlags(flags)
        , Pixels(pixels ? pixels->Pixels : nullptr)
        , m_SharedPixels(pixels)
    {
    }

    Image(const unsigned width, const unsigned height, std::shared_ptr<SharedPixels>&& pixels, const Flags flags)
        : Width(width)
        , Height(height)
        , ImageFlags(flags)
        , Pixels(pixels ? pixels->Pixels : nullptr)
        , m_SharedPixels(std::move(pixels))
    {
    }

    std::shared_ptr<SharedPixels> m_SharedPixels;
};
