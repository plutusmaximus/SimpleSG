#pragma once

#include "Error.h"
#include <compare>
#include <utility>

/// @brief Unique key for caching resources.
class CacheKey
{
    friend std::hash<CacheKey>;

public:

    explicit CacheKey(const char* key)
        : m_Key(key ? key : "")
        , m_HashCode(std::hash<std::string_view>()(key ? key : ""))
    {
        // CacheKey must not be empty.
        eassert(key && key[0] != '\0');
    }

    explicit CacheKey(const imstring& key)
        : m_Key(key)
        , m_HashCode(std::hash<std::string_view>()(key))
    {
        // CacheKey must not be empty.
        eassert(!key.empty());
    }

    bool operator==(const CacheKey& other) const
    {
        return m_HashCode == other.m_HashCode && m_Key == other.m_Key;
    }

    bool operator!=(const CacheKey& other) const
    {
        return !(*this == other);
    }

    bool operator<(const CacheKey& other) const
    {
        if(m_HashCode < other.m_HashCode)
        {
            return true;
        }
        else if(m_HashCode > other.m_HashCode)
        {
            return false;
        }
        else
        {
            return m_Key < other.m_Key;
        }
    }

    friend std::strong_ordering operator<=>(const CacheKey& a, const CacheKey& b) noexcept
    {
        if(a.m_HashCode != b.m_HashCode)
        {
            return a.m_HashCode <=> b.m_HashCode;
        }
        else
        {
            return a.m_Key <=> b.m_Key;
        }
    }

    const imstring& ToString() const
    {
        return m_Key;
    }

private:
    CacheKey() = delete;

    imstring m_Key;
    size_t m_HashCode;
};

/// @brief Enable hashing of CacheKey for use in unordered containers.
template<>
struct std::hash<CacheKey>
{
    std::size_t operator()(const CacheKey& key) const noexcept
    {
        return key.m_HashCode;
    }
};
