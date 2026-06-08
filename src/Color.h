#pragma once

#include "AssertHelper.h"
#include <algorithm>
#include <span>
#include <string>

/// @brief RGBA color representation.
template<typename T>
class RgbaColor
{
public:

    using ValueType = T;

    static constexpr T kMaxValue = std::is_integral_v<T> ? static_cast<T>(255) : static_cast<T>(1);
    static constexpr T kMinValue = 0;

    constexpr RgbaColor() = default;

    constexpr RgbaColor(const T inR, const T inG, const T inB) noexcept
        : RgbaColor(inR, inG, inB, 1)
    {
    }

    constexpr RgbaColor(const T inR, const T inG, const T inB, const T inA) noexcept
        : r(inR), g(inG), b(inB), a(inA)
    {
    }

    template<typename U>
    constexpr static T Clamp(const U value) noexcept
    {
        const U clampedValue = std::clamp(value, static_cast<U>(kMinValue), static_cast<U>(kMaxValue));
        return static_cast<T>(clampedValue);
    }

    /// @brief  Conversion constructor.
    template<typename U>
    constexpr explicit RgbaColor(const RgbaColor<U>& other) noexcept;

    /// @brief Converts the color to a hexadecimal string representation - #RRGGBBAA
    [[nodiscard]] std::string ToHexString() const;

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
constexpr RgbaColor<uint8_t>::RgbaColor(const uint8_t inR, const uint8_t inG, const uint8_t inB) noexcept
    : RgbaColor<uint8_t>(inR, inG, inB, kMaxValue)
{
}

/// @brief Specialization for converting from float to uint8_t.
template<>
template<>
constexpr RgbaColor<uint8_t>::RgbaColor(const RgbaColor<float>& other) noexcept
    : r(Clamp(other.r * kMaxValue))
    , g(Clamp(other.g * kMaxValue))
    , b(Clamp(other.b * kMaxValue))
    , a(Clamp(other.a * kMaxValue))
{
}

/// @brief Specialization for float with clamping between 0.0 and 1.0.
template<>
constexpr RgbaColor<float>::RgbaColor(const float inR, const float inG, const float inB, const float inA) noexcept
    : r(Clamp(inR)), g(Clamp(inG)), b(Clamp(inB)), a(Clamp(inA))
{
    MLG_ASSERT(inR >= kMinValue && inR <= kMaxValue);
    MLG_ASSERT(inG >= kMinValue && inG <= kMaxValue);
    MLG_ASSERT(inB >= kMinValue && inB <= kMaxValue);
    MLG_ASSERT(inA >= kMinValue && inA <= kMaxValue);
}

/// @brief Specialization for converting from uint8_t to float.
template<>
template<>
constexpr RgbaColor<float>::RgbaColor(const RgbaColor<uint8_t>& other) noexcept
    : r(Clamp(static_cast<float>(other.r) / RgbaColor<uint8_t>::kMaxValue))
    , g(Clamp(static_cast<float>(other.g) / RgbaColor<uint8_t>::kMaxValue))
    , b(Clamp(static_cast<float>(other.b) / RgbaColor<uint8_t>::kMaxValue))
    , a(Clamp(static_cast<float>(other.a) / RgbaColor<uint8_t>::kMaxValue))
{
}

template<>
inline std::string
RgbaColor<uint8_t>::ToHexString() const
{
    constexpr char kHexDigits[] = "0123456789ABCDEF";
    constexpr size_t kMask = 0x0F;
    constexpr const char* kInitialString = "#00000000";
    std::string hexString(kInitialString);
    size_t offset = 1;
    hexString[offset++] = kHexDigits[(r >> 4) & kMask];
    hexString[offset++] = kHexDigits[r & kMask];
    hexString[offset++] = kHexDigits[(g >> 4) & kMask];
    hexString[offset++] = kHexDigits[g & kMask];
    hexString[offset++] = kHexDigits[(b >> 4) & kMask];
    hexString[offset++] = kHexDigits[b & kMask];
    hexString[offset++] = kHexDigits[(a >> 4) & kMask];
    hexString[offset++] = kHexDigits[a & kMask];
    return hexString;
}

template<>
inline std::string RgbaColor<float>::ToHexString() const
{
    return RgbaColor<uint8_t>(*this).ToHexString();
}

/// @brief User-defined literal to convert a hex color code to an RGBA color.
constexpr RgbaColor<uint8_t> operator""_rgba(const char* str, const size_t len)
{
    auto from_hex = [](char c) -> uint8_t
    {
        constexpr char kLowerHexOffset = 'a' - 10;
        constexpr char kUpperHexOffset = 'A' - 10;

        if(c >= '0' && c <= '9')
        {
            return static_cast<uint8_t>(c - '0');
        }
        if(c >= 'a' && c <= 'f')
        {
            return static_cast<uint8_t>(c - kLowerHexOffset);
        }
        if(c >= 'A' && c <= 'F')
        {
            return static_cast<uint8_t>(c - kUpperHexOffset);
        }
        return 0;
    };

    const std::span strSpan(str, len);
    const size_t offset = (len > 0 && strSpan[0] == '#') ? 1 : 0;
    const size_t digits = len - offset;

    // NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
    if (digits == 3)
    {
        // Shorthand RGB (e.g., #F0A), expand to full form which is #FF00AA
        return RgbaColor<uint8_t>(
            static_cast<uint8_t>((from_hex(strSpan[offset]) << 4) | from_hex(strSpan[offset])),
            static_cast<uint8_t>((from_hex(strSpan[offset+1]) << 4) | from_hex(strSpan[offset+1])),
            static_cast<uint8_t>((from_hex(strSpan[offset+2]) << 4) | from_hex(strSpan[offset+2]))
        );
    }
    if (digits == 6)
    {
        // Full RGB (e.g., #FF00AA)
        return RgbaColor<uint8_t>(
            static_cast<uint8_t>((from_hex(strSpan[offset]) << 4) | from_hex(strSpan[offset+1])),
            static_cast<uint8_t>((from_hex(strSpan[offset+2]) << 4) | from_hex(strSpan[offset+3])),
            static_cast<uint8_t>((from_hex(strSpan[offset+4]) << 4) | from_hex(strSpan[offset+5]))
        );
    }
    if (digits == 8)
    {
        // Full RGBA (e.g., #FF00AAFF)
        return RgbaColor<uint8_t>(
            static_cast<uint8_t>((from_hex(strSpan[offset]) << 4) | from_hex(strSpan[offset+1])),
            static_cast<uint8_t>((from_hex(strSpan[offset+2]) << 4) | from_hex(strSpan[offset+3])),
            static_cast<uint8_t>((from_hex(strSpan[offset+4]) << 4) | from_hex(strSpan[offset+5])),
            static_cast<uint8_t>((from_hex(strSpan[offset+6]) << 4) | from_hex(strSpan[offset+7]))
        );
    }
    // NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

    return RgbaColor<uint8_t>(); // default
}
