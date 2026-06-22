#pragma once

#include <format>
#include <cstddef>
#include <string_view>
#include <vector>

class StringArena;

/// @brief A handle to a string stored in a StringArena.
/// The StringHandle lifetime is tied to the StringArena that created it.
/// Make sure to not use a StringHandle after the StringArena that created it has been destroyed.
class StringHandle
{
public:

    StringHandle() = default;

    const char* c_str() const { return m_StringView.data(); }

    friend bool operator==(const StringHandle& lhs, const StringHandle& rhs)
    {
        return lhs.m_StringView == rhs.m_StringView;
    }

    friend bool operator!=(const StringHandle& lhs, const StringHandle& rhs)
    {
        return !(lhs == rhs);
    }

    friend bool operator==(const StringHandle& lhs, const std::string_view& rhs)
    {
        return lhs.m_StringView == rhs;
    }

    friend bool operator!=(const StringHandle& lhs, const std::string_view& rhs)
    {
        return !(lhs == rhs);
    }

    friend bool operator==(const std::string_view& lhs, const StringHandle& rhs)
    {
        return lhs == rhs.m_StringView;
    }

    friend bool operator!=(const std::string_view& lhs, const StringHandle& rhs)
    {
        return !(lhs == rhs);
    }

    friend auto operator<=>(const StringHandle& lhs, const StringHandle& rhs)
    {
        return lhs.m_StringView <=> rhs.m_StringView;
    }

    friend auto operator<=>(const StringHandle& lhs, const std::string_view& rhs)
    {
        return lhs.m_StringView <=> rhs;
    }

    friend auto operator<=>(const std::string_view& lhs, const StringHandle& rhs)
    {
        return lhs <=> rhs.m_StringView;
    }

    operator std::string_view() const // NOLINT(google-explicit-constructor)
    {
        return m_StringView;
    }

private:
    friend class StringArena;
    friend struct std::hash<StringHandle>;

    explicit StringHandle(const std::string_view& stringView)
        : m_StringView(stringView),
          m_HashCode(std::hash<const char*>{}(stringView.data()))
    {
    }

    static constexpr const char kDefaultEmptyString = '\0';

    std::string_view m_StringView{&kDefaultEmptyString, 1};
    size_t m_HashCode{ 0 };
};

/// @brief A simple arena allocator for strings. Strings are stored in contiguous chunks of memory.
/// This is optimized for the case where a large number of strings are created and have similar lifetimes.
/// The arena will allocate new chunks as needed, but does not support deallocation of individual strings.
/// Instead, the entire arena can be destroyed to free all memory.
///
/// *** Not thread safe. ***
class StringArena
{
public:
    StringArena()
        : StringArena(kDefaultChunkSize)
    {
    }

    /// @brief Constructs a StringArena with the specified chunk size.
    /// Attempts to allocate a string larger than the chunk size will abort the program.
    explicit StringArena(const size_t chunkSize);

    StringArena(const StringArena&) = delete;
    ~StringArena() = default;
    StringArena& operator=(const StringArena&) = delete;
    StringArena(StringArena&&) = default;
    StringArena& operator=(StringArena&&) = default;

    StringHandle NewString(const std::string_view& stringView);

    // For debugging purposes, get the number of allocated chunks and strings.
    // We can't use perf counters for this since the StringArena is used by the perf counters themselves.
    static size_t GetTotalStringSize() { return sm_TotalStringSize; }
    static size_t GetChunkCount() { return sm_ChunkCount; }
    static size_t GetStringCount() { return sm_StringCount; }

private:
    static constexpr size_t kDefaultChunkSize = 1024;

    struct Chunk
    {
        explicit Chunk(const size_t chunkSize) { Buffer.reserve(chunkSize); }

        std::vector<char> Buffer;
    };

    size_t m_ChunkSize;
    std::vector<Chunk> m_Chunks;

    static size_t sm_TotalStringSize;
    static size_t sm_ChunkCount;
    static size_t sm_StringCount;
};

template<>
struct std::formatter<StringHandle> : std::formatter<std::string_view>
{
    auto format(const StringHandle& stringHandle, auto& context) const
    {
        return std::formatter<std::string_view>::format(stringHandle.c_str(), context);
    }
};

template<>
struct std::hash<StringHandle>
{
    size_t operator()(const StringHandle& stringHandle) const { return stringHandle.m_HashCode; }
};