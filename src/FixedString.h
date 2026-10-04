#pragma once

#include "AssertHelper.h"

#include <algorithm>
#include <cstddef>
#include <format>
#include <spdlog/fmt/fmt.h>
#include <string_view>

/// A fixed-size string class that stores a string of up to N-1 characters and a null terminator.
template<size_t N>
class FixedString
{
    template<size_t M>
    friend class FixedString;

public:
    /// The size of the storage buffer, including the null terminator.
    static constexpr size_t kStorageSize = N;

    /// The maximum number of characters that can be stored, excluding the null terminator.
    static constexpr size_t kMaxLength = N - 1;

    static_assert(kStorageSize > 0, "FixedString size must be greater than 0");

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    FixedString()
    {
        m_Chars[0] = '\0';
        m_Hash = ComputeHash(*this); // NOLINT(cppcoreguidelines-prefer-member-initializer)
    }

    template<size_t M>
        requires(M <= kStorageSize)
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    constexpr explicit FixedString(FixedString<M> value)
        : m_Length(value.size())
    {
        for(size_t i = 0; i < m_Length; ++i)
        {
            m_Chars[i] = value.m_Chars[i];
        }
        m_Chars[m_Length] = '\0';

        m_Hash = ComputeHash(*this);
    }

    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
    explicit FixedString(const std::string_view str)
        : m_Length((str.size() < kStorageSize) ? str.size() : kMaxLength)
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

        m_Length = (str.size() < kStorageSize) ? str.size() : kMaxLength;

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

    size_t GetHashCode() const { return m_Hash; }

    /// Formats a message into a FixedString.
    /// If the formatted message exceeds the buffer size, it will be truncated and an
    /// ellipsis will be appended.
    template<typename... Args>
    static FixedString Format(fmt::format_string<Args...> fmtStr, Args&&... args)
    {
        FixedString fs;
        auto result =
            fmt::format_to_n(&fs.m_Chars[0], kMaxLength, fmtStr, std::forward<Args>(args)...);

        if(result.size > kMaxLength && kMaxLength >= 3)
        {
            fs.m_Chars[kMaxLength - 3] = '.';
            fs.m_Chars[kMaxLength - 2] = '.';
            fs.m_Chars[kMaxLength - 1] = '.';
        }

        const size_t formattedSize = std::min(result.size, kMaxLength);
        fs.m_Length = formattedSize;
        fs.m_Chars[formattedSize] = '\0';
        fs.m_Hash = ComputeHash(fs);

        return fs;
    }

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator std::string_view() const { return std::string_view(&m_Chars[0], m_Length); }

    friend auto operator<=>(const FixedString& a, const FixedString& b)
    {
        return std::string_view(a) <=> std::string_view(b);
    }

    friend bool operator==(const FixedString& a, const FixedString& b)
    {
        if(a.GetHashCode() == b.GetHashCode())
        {
            return std::string_view(a) == std::string_view(b);
        }

        return false;
    }

    friend bool operator!=(const FixedString& a, const FixedString& b) { return !(a == b); }

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

/// Hash specialization for FixedString to be used in unordered containers.
template<size_t N>
struct std::hash<FixedString<N>>
{
    size_t operator()(const FixedString<N>& str) const noexcept { return str.GetHashCode(); }
};