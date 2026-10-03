#pragma once

#include "FilePathHelper.h"
#include "FixedString.h"
#include "Result.h"

#include <cstddef>
#include <format>
#include <string_view>

class DirectoryPath;
class FilePath;

/// A validated file path relative to the application's working directory.
class RelativeFilePath final
{
public:
    static constexpr size_t kStorageSize = 128;
    static constexpr size_t kMaxLength = kStorageSize - 1;

    RelativeFilePath() = delete;

    static Result<RelativeFilePath> Create(std::string_view path);

    const char* c_str() const noexcept { return m_Value.c_str(); }

    size_t size() const noexcept { return m_Value.size(); }

    friend bool operator==(const RelativeFilePath&, const RelativeFilePath&) = default;

    friend Result<FilePath> Join(const DirectoryPath& directory, const RelativeFilePath& file);

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator std::string_view() const noexcept { return m_Value; }

    size_t GetHashCode() const noexcept { return m_Value.GetHashCode(); }

private:
    explicit RelativeFilePath(FixedString<kStorageSize> value);

    FixedString<kStorageSize> m_Value;
};

/// A validated directory prefix relative to the application's working directory.
/// The current directory is represented by "./".
class DirectoryPath final
{
public:
    DirectoryPath() = delete;

    static DirectoryPath Current();
    static Result<DirectoryPath> Create(std::string_view path);

    const char* c_str() const noexcept { return m_Value.c_str(); }

    size_t size() const noexcept { return m_Value.size(); }

    /// Joins a directory prefix and file path without allocating.
    /// Returns failure if their combined length exceeds FilePath::kMaxLength.
    Result<FilePath> Join(const RelativeFilePath& file) const;

    /// Joins a directory prefix and file path without allocating.
    /// Returns failure if their combined length exceeds FilePath::kMaxLength.
    Result<FilePath> Join(const std::string_view file) const;

    friend bool operator==(const DirectoryPath&, const DirectoryPath&) = default;

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator std::string_view() const noexcept { return m_Value; }

    size_t GetHashCode() const noexcept { return m_Value.GetHashCode(); }

private:
    explicit DirectoryPath(FixedString<RelativeFilePath::kStorageSize> value);

    FixedString<RelativeFilePath::kStorageSize> m_Value;
};

// A validated file path that can be longer than the standard FilePath.
// The result of joining a DirectorPath and a FilePath.
// A LongFilePath can't be further joined with another path.
class FilePath final
{
public:
    static constexpr size_t kStorageSize = RelativeFilePath::kStorageSize * 2;
    static constexpr size_t kMaxLength = kStorageSize - 1;

    FilePath() = delete;

    const char* c_str() const noexcept { return m_Value.c_str(); }

    size_t size() const noexcept { return m_Value.size(); }

    /// Returns the stem (filename without the directory path) of the file path.
    std::string_view GetStem() const noexcept { return FilePathHelper::GetStem(m_Value); }

    friend bool operator==(const FilePath&, const FilePath&) = default;

    friend Result<FilePath> Join(const DirectoryPath& directory, const RelativeFilePath& file);

    // NOLINTNEXTLINE(google-explicit-constructor)
    operator std::string_view() const noexcept { return m_Value; }

    size_t GetHashCode() const noexcept { return m_Value.GetHashCode(); }

private:
    explicit FilePath(FixedString<kStorageSize> value);

    FixedString<kStorageSize> m_Value;
};

/// Joins a directory prefix and file path without allocating.
/// Returns failure if their combined length exceeds FilePath::kMaxLength.
Result<FilePath> Join(const DirectoryPath& directory, const RelativeFilePath& file);

/// format_as is used by fmt::format.
inline std::string_view
format_as(const RelativeFilePath& path) noexcept
{
    return std::string_view(path);
}

inline std::string_view
format_as(const FilePath& path) noexcept
{
    return std::string_view(path);
}

inline std::string_view
format_as(const DirectoryPath& path) noexcept
{
    return std::string_view(path);
}

/// Formatter specialization for FilePath to be used with std::format.
template<>
struct std::formatter<RelativeFilePath> : std::formatter<std::string_view>
{
    auto format(const RelativeFilePath& path, auto& context) const
    {
        return std::formatter<std::string_view>::format(std::string_view(path), context);
    }
};

/// Formatter specialization for LongFilePath to be used with std::format.
template<>
struct std::formatter<FilePath> : std::formatter<std::string_view>
{
    auto format(const FilePath& path, auto& context) const
    {
        return std::formatter<std::string_view>::format(std::string_view(path), context);
    }
};

/// Formatter specialization for DirectoryPath to be used with std::format.
template<>
struct std::formatter<DirectoryPath> : std::formatter<std::string_view>
{
    auto format(const DirectoryPath& path, auto& context) const
    {
        return std::formatter<std::string_view>::format(std::string_view(path), context);
    }
};

/// Hasher specialization for FilePath to be used in unordered containers.
template<>
struct std::hash<RelativeFilePath>
{
    size_t operator()(const RelativeFilePath& path) const noexcept { return path.GetHashCode(); }
};

/// Hasher specialization for LongFilePath to be used in unordered containers.
template<>
struct std::hash<FilePath>
{
    size_t operator()(const FilePath& path) const noexcept { return path.GetHashCode(); }
};

/// Hasher specialization for DirectoryPath to be used in unordered containers.
template<>
struct std::hash<DirectoryPath>
{
    size_t operator()(const DirectoryPath& path) const noexcept { return path.GetHashCode(); }
};