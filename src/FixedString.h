#pragma once

#include "AssertHelper.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <initializer_list>
#include <optional>
#include <string_view>

/// A fixed-size string class that stores a string of up to N-1 characters and a null terminator.
/// Used instead of std::string for strings at rest (e.g. member vars).
template<size_t N>
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
class FixedString
{
public:
    static constexpr size_t kStorageSize = N;
    static constexpr size_t kCapacity = N - 1;

    static_assert(kStorageSize > 0, "FixedString size must be greater than 0");

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    FixedString()
    {
        m_Chars[0] = '\0';
        m_Hash = ComputeHash(*this); // NOLINT(cppcoreguidelines-prefer-member-initializer)
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    explicit FixedString(const std::string_view str)
        : m_Length((str.size() < kStorageSize) ? str.size() : kCapacity)
    {
        MLG_ASSERT(str.size() < kStorageSize);

        for(size_t i = 0; i < m_Length; ++i)
        {
            m_Chars[i] = str[i];
        }
        m_Chars[m_Length] = '\0';

        m_Hash = ComputeHash(*this);
    }

    FixedString& operator=(const std::string_view str)
    {
        MLG_ASSERT(str.size() < kStorageSize);

        m_Length = (str.size() < kStorageSize) ? str.size() : kCapacity;

        for(size_t i = 0; i < m_Length; ++i)
        {
            m_Chars[i] = str[i];
        }
        m_Chars[m_Length] = '\0';

        m_Hash = ComputeHash(*this);

        return *this;
    }

    size_t size() const { return m_Length; }

    bool empty() const { return m_Length == 0; }

    const char* c_str() const { return &m_Chars[0]; }

    const char* data() const { return &m_Chars[0]; }

    /// Attempts to concatenate the given parts into a FixedString.
    /// Returns std::nullopt if the concatenated string would exceed the capacity.
    [[nodiscard]] static std::optional<FixedString> TryCat(
        const std::initializer_list<std::string_view> parts)
    {
        size_t remaining = kCapacity;
        for(const std::string_view view : parts)
        {
            if(!MLG_VERIFY(view.size() <= remaining))
            {
                return std::nullopt;
            }
            remaining -= view.size();
        }

        FixedString<kStorageSize> result;
        char* out = &result.m_Chars[0];
        for(const std::string_view view : parts)
        {
            out = std::ranges::copy(view, out).out;
        }
        *out = '\0';
        result.m_Length = kCapacity - remaining;
        result.m_Hash = ComputeHash(result);
        return result;
    }

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator std::string_view() const { return std::string_view(&m_Chars[0], m_Length); }

    friend auto operator<=>(const FixedString& lhs, const FixedString& rhs)
    {
        return std::string_view(lhs) <=> std::string_view(rhs);
    }

    friend bool operator==(const FixedString& lhs, const FixedString& rhs)
    {
        if(lhs.GetHashCode() == rhs.GetHashCode())
        {
            return std::string_view(lhs) == std::string_view(rhs);
        }

        return false;
    }

    friend bool operator!=(const FixedString& lhs, const FixedString& rhs) { return !(lhs == rhs); }

    size_t GetHashCode() const { return m_Hash; }

private:
    static size_t ComputeHash(const std::string_view str)
    {
        return std::hash<std::string_view>{}(str);
    }

    char m_Chars[kStorageSize];
    size_t m_Length{ 0 };
    size_t m_Hash{ 0 };
};

/// format_as is used by fmt::format for FixedString.
template<size_t N>
inline std::string_view
format_as(const FixedString<N>& str) noexcept
{
    return std::string_view(str);
}

/// Formatter specialization for FixedString to be used with std::format.
template<size_t N>
struct std::formatter<FixedString<N>> : std::formatter<std::string_view>
{
    auto format(const FixedString<N>& str, auto& context) const
    {
        return std::formatter<std::string_view>::format(std::string_view(str), context);
    }
};

/// Hasher specialization for FixedString to be used in unordered containers.
template<size_t N>
struct std::hash<FixedString<N>>
{
    size_t operator()(const FixedString<N>& str) const noexcept { return str.GetHashCode(); }
};
