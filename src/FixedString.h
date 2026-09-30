#pragma once

#include "AssertHelper.h"

#include <cstddef>
#include <format>
#include <string_view>

/// A fixed-size string class that stores a string of up to N-1 characters and a null terminator.
/// Used instead of std::string for strings at rest (e.g. member vars).
/// Typically FixedString is not passed as a parameter - pass std::string_view instead for
/// maximum compatibility.
template<size_t N>
// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
class FixedString
{
    static_assert(N > 0, "FixedString size must be greater than 0");

public:
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    FixedString()
    {
        Chars[0] = '\0';
        m_Hash = ComputeHash(*this); // NOLINT(cppcoreguidelines-prefer-member-initializer)
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    explicit FixedString(const std::string_view str)
        : m_Length((str.size() < N) ? str.size() : N - 1)
    {
        MLG_ASSERT(str.size() < N);

        for(size_t i = 0; i < m_Length; ++i)
        {
            Chars[i] = str[i];
        }
        Chars[m_Length] = '\0';

        m_Hash = ComputeHash(*this);
    }

    FixedString& operator=(const std::string_view str)
    {
        MLG_ASSERT(str.size() < N);

        m_Length = (str.size() < N) ? str.size() : N - 1;

        for(size_t i = 0; i < m_Length; ++i)
        {
            Chars[i] = str[i];
        }
        Chars[m_Length] = '\0';

        m_Hash = ComputeHash(*this);

        return *this;
    }

    size_t size() const { return m_Length; }

    bool empty() const { return m_Length == 0; }

    const char* c_str() const { return &Chars[0]; }

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator std::string_view() const { return std::string_view(&Chars[0], m_Length); }

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

    size_t GetHashCode() const
    {
        return m_Hash;
    }

private:
    static size_t ComputeHash(const std::string_view str)
    {
        return std::hash<std::string_view>{}(str);
    }

    char Chars[N];
    size_t m_Length{ 0 };
    size_t m_Hash{ 0 };
};

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