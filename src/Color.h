#pragma once

#include "AssertHelper.h"
#include <algorithm>

/// @brief RGBA color representation.
template<typename T>
class RgbaColor
{
public:

    using ValueType = T;

    constexpr RgbaColor() = default;

    constexpr RgbaColor(const T inR, const T inG, const T inB) noexcept
        : RgbaColor(inR, inG, inB, 1)
    {
    }

    constexpr RgbaColor(const T inR, const T inG, const T inB, const T inA) noexcept
        : r(inR), g(inG), b(inB), a(inA)
    {
    }

    /// @brief  Conversion constructor.
    template<typename U>
    constexpr RgbaColor(const RgbaColor<U>& other) noexcept;

    /// @brief Converts the color to a hexadecimal string representation - #RRGGBBAA
    std::string ToHexString() const noexcept;

    constexpr friend bool operator==(const RgbaColor& colorA, const RgbaColor& colorB) noexcept
    {
        return colorA.r == colorB.r && colorA.g == colorB.g && colorA.b == colorB.b &&
               colorA.a == colorB.a;
    }

    T r{0}, g{0}, b{0}, a{0};
};

using RgbaColorf = RgbaColor<float>;
using RgbaColoru8 = RgbaColor<uint8_t>;

/// @brief Specialization for uint8_t with default alpha of 255.
template<>
inline constexpr RgbaColor<uint8_t>::RgbaColor(const uint8_t inR, const uint8_t inG, const uint8_t inB) noexcept
    : RgbaColor<uint8_t>(inR, inG, inB, 255)
{
}

/// @brief Specialization for converting from float to uint8_t.
template<>
template<>
inline RgbaColor<uint8_t>::RgbaColor(const RgbaColor<float>& other) noexcept
    : r(static_cast<uint8_t>(std::clamp(other.r * 255.0f, 0.0f, 255.0f)))
    , g(static_cast<uint8_t>(std::clamp(other.g * 255.0f, 0.0f, 255.0f)))
    , b(static_cast<uint8_t>(std::clamp(other.b * 255.0f, 0.0f, 255.0f)))
    , a(static_cast<uint8_t>(std::clamp(other.a * 255.0f, 0.0f, 255.0f)))
{
}

/// @brief Specialization for float with clamping between 0.0 and 1.0.
template<>
inline constexpr RgbaColor<float>::RgbaColor(const float inR, const float inG, const float inB, const float inA) noexcept
    : r(std::clamp(inR, 0.0f, 1.0f)), g(std::clamp(inG, 0.0f, 1.0f)), b(std::clamp(inB, 0.0f, 1.0f)), a(std::clamp(inA, 0.0f, 1.0f))
{
    MLG_ASSERT(inR >= 0 && inR <= 1);
    MLG_ASSERT(inG >= 0 && inG <= 1);
    MLG_ASSERT(inB >= 0 && inB <= 1);
    MLG_ASSERT(inA >= 0 && inA <= 1);
}

/// @brief Specialization for converting from uint8_t to float.
template<>
template<>
inline RgbaColor<float>::RgbaColor(const RgbaColor<uint8_t>& other) noexcept
    : r(static_cast<float>(other.r) / 255.0f)
    , g(static_cast<float>(other.g) / 255.0f)
    , b(static_cast<float>(other.b) / 255.0f)
    , a(static_cast<float>(other.a) / 255.0f)
{
}

template<>
inline std::string RgbaColor<uint8_t>::ToHexString() const noexcept
{
    char buf[10];
    snprintf(buf, sizeof(buf), "#%02X%02X%02X%02X", r, g, b, a);
    return std::string(buf);
}

template<>
inline std::string RgbaColor<float>::ToHexString() const noexcept
{
    return RgbaColor<uint8_t>(*this).ToHexString();
}

/// @brief User-defined literal to convert a hex color code to an RGBA color.
constexpr RgbaColor<uint8_t> operator""_rgba(const char* str, const size_t len)
{
    auto from_hex = [](char c) -> uint8_t
    {
        if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
        return 0;
    };

    size_t offset = (len > 0 && str[0] == '#') ? 1 : 0;
    size_t digits = len - offset;

    if (digits == 3)
    {
        // Shorthand RGB (e.g., #F0A), expand to full form which is #FF00AA
        return RgbaColor<uint8_t>(
            static_cast<uint8_t>((from_hex(str[offset]) << 4) | from_hex(str[offset])),
            static_cast<uint8_t>((from_hex(str[offset+1]) << 4) | from_hex(str[offset+1])),
            static_cast<uint8_t>((from_hex(str[offset+2]) << 4) | from_hex(str[offset+2]))
        );
    }
    if (digits == 6)
    {
        // Full RGB (e.g., #FF00AA)
        return RgbaColor<uint8_t>(
            static_cast<uint8_t>((from_hex(str[offset]) << 4) | from_hex(str[offset+1])),
            static_cast<uint8_t>((from_hex(str[offset+2]) << 4) | from_hex(str[offset+3])),
            static_cast<uint8_t>((from_hex(str[offset+4]) << 4) | from_hex(str[offset+5]))
        );
    }
    if (digits == 8)
    {
        // Full RGBA (e.g., #FF00AAFF)
        return RgbaColor<uint8_t>(
            static_cast<uint8_t>((from_hex(str[offset]) << 4) | from_hex(str[offset+1])),
            static_cast<uint8_t>((from_hex(str[offset+2]) << 4) | from_hex(str[offset+3])),
            static_cast<uint8_t>((from_hex(str[offset+4]) << 4) | from_hex(str[offset+5])),
            static_cast<uint8_t>((from_hex(str[offset+6]) << 4) | from_hex(str[offset+7]))
        );
    }
    return RgbaColor<uint8_t>(); // default
}
