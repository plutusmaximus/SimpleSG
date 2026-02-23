#pragma once

#include "Error.h"
#include "imstring.h"
#include <algorithm>

/// @brief RGBA color representation.
template<typename T>
class RgbaColor
{
public:
    constexpr RgbaColor()
        : RgbaColor(0, 0, 0, 0)
    {
    }

    constexpr RgbaColor(const T inR, const T inG, const T inB)
        : RgbaColor(inR, inG, inB, 1)
    {
    }

    constexpr RgbaColor(const T inR, const T inG, const T inB, const T inA)
        : r(inR), g(inG), b(inB), a(inA)
    {
    }

    /// @brief  Conversion constructor.
    template<typename U>
    constexpr RgbaColor(const RgbaColor<U>& other);

    /// @brief Converts the color to a hexadecimal string representation - #RRGGBBAA
    imstring ToHexString() const;

    bool operator==(const RgbaColor& other) const
    {
        return r == other.r && g == other.g && b == other.b && a == other.a;
    }

    bool operator!=(const RgbaColor& other) const
    {
        return !(*this == other);
    }

    T r, g, b, a;
};

using RgbaColorf = RgbaColor<float>;
using RgbaColoru8 = RgbaColor<uint8_t>;

/// @brief Specialization for uint8_t with default alpha of 255.
inline constexpr RgbaColor<uint8_t>::RgbaColor(const uint8_t inR, const uint8_t inG, const uint8_t inB)
    : RgbaColor<uint8_t>(inR, inG, inB, 255)
{
}

/// @brief Specialization for converting from float to uint8_t.
template<>
template<>
inline RgbaColor<uint8_t>::RgbaColor(const RgbaColor<float>& other)
    : r(static_cast<uint8_t>(std::clamp(other.r * 255.0f, 0.0f, 255.0f)))
    , g(static_cast<uint8_t>(std::clamp(other.g * 255.0f, 0.0f, 255.0f)))
    , b(static_cast<uint8_t>(std::clamp(other.b * 255.0f, 0.0f, 255.0f)))
    , a(static_cast<uint8_t>(std::clamp(other.a * 255.0f, 0.0f, 255.0f)))
{
}

/// @brief Specialization for float with clamping between 0.0 and 1.0.
inline constexpr RgbaColor<float>::RgbaColor(const float inR, const float inG, const float inB, const float inA)
    : r(std::clamp(inR, 0.0f, 1.0f)), g(std::clamp(inG, 0.0f, 1.0f)), b(std::clamp(inB, 0.0f, 1.0f)), a(std::clamp(inA, 0.0f, 1.0f))
{
    eassert(inR >= 0 && inR <= 1);
    eassert(inG >= 0 && inG <= 1);
    eassert(inB >= 0 && inB <= 1);
    eassert(inA >= 0 && inA <= 1);
}

/// @brief Specialization for converting from uint8_t to float.
template<>
template<>
inline RgbaColor<float>::RgbaColor(const RgbaColor<uint8_t>& other)
    : r(static_cast<float>(other.r) / 255.0f)
    , g(static_cast<float>(other.g) / 255.0f)
    , b(static_cast<float>(other.b) / 255.0f)
    , a(static_cast<float>(other.a) / 255.0f)
{
}

inline imstring RgbaColor<uint8_t>::ToHexString() const
{
    char buf[10];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", r, g, b, a);
    return imstring(buf);
}

inline imstring RgbaColor<float>::ToHexString() const
{
    return RgbaColor<uint8_t>(*this).ToHexString();
}

/// @brief User-defined literal to convert a hex color code to an RGBA color.
constexpr RgbaColor<uint8_t> operator"" _rgba(const char* str, size_t len)
{
    auto from_hex = [](char c) -> uint8_t
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
    };

    size_t offset = (len > 0 && str[0] == '#') ? 1 : 0;
    size_t digits = len - offset;

    if (digits == 3)
    {
        // Shorthand RGB (e.g., #F0A), expand to full form which is #FF00AA
        return RgbaColor<uint8_t>(
            (from_hex(str[offset]) << 4) | from_hex(str[offset]),
            (from_hex(str[offset+1]) << 4) | from_hex(str[offset+1]),
            (from_hex(str[offset+2]) << 4) | from_hex(str[offset+2])
        );
    }
    if (digits == 6)
    {
        // Full RGB (e.g., #FF00AA)
        return RgbaColor<uint8_t>(
            (from_hex(str[offset]) << 4) | from_hex(str[offset+1]),
            (from_hex(str[offset+2]) << 4) | from_hex(str[offset+3]),
            (from_hex(str[offset+4]) << 4) | from_hex(str[offset+5])
        );
    }
    if (digits == 8)
    {
        // Full RGBA (e.g., #FF00AAFF)
        return RgbaColor<uint8_t>(
            (from_hex(str[offset]) << 4) | from_hex(str[offset+1]),
            (from_hex(str[offset+2]) << 4) | from_hex(str[offset+3]),
            (from_hex(str[offset+4]) << 4) | from_hex(str[offset+5]),
            (from_hex(str[offset+6]) << 4) | from_hex(str[offset+7])
        );
    }
    return RgbaColor<uint8_t>(); // default
}
