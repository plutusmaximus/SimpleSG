#pragma once

#include <cstddef>
#include <string_view>

namespace FilePathHelper
{
/// Returns true when the character is a path separator.
constexpr bool
IsSeparator(char character) noexcept
{
    return character == '/' || character == '\\';
}

/// Returns the directory containing the final path component.
/// Trailing separators are removed before finding the parent.
///
/// Examples:
/// "assets/textures/brick.png" returns "assets/textures".
/// "assets/textures/" returns "assets".
/// "brick.png" returns an empty view.
constexpr std::string_view
GetParent(std::string_view path) noexcept
{
    if(path.empty())
    {
        return {};
    }

    while(path.size() > 1 && IsSeparator(path.back()))
    {
        path.remove_suffix(1);
    }

    const std::size_t separator = path.find_last_of("/\\");

    if(separator == std::string_view::npos)
    {
        return {};
    }

    if(separator == 0)
    {
        return path.substr(0, 1);
    }

    if(separator == 2 && path.size() >= 3 && path[1] == ':')
    {
        return path.substr(0, 3);
    }

    return path.substr(0, separator);
}

/// Returns the final path component, including its extension.
/// Returns an empty view when the path ends with a separator.
///
/// Example:
/// "assets/textures/brick.png" returns "brick.png".
constexpr std::string_view
GetFileName(std::string_view path) noexcept
{
    const std::size_t separator = path.find_last_of("/\\");

    if(separator == std::string_view::npos)
    {
        return path;
    }

    return path.substr(separator + 1);
}

/// Returns the final extension, including its leading dot.
/// Returns an empty view when the filename has no extension.
/// A filename such as ".gitignore" is treated as having no extension.
///
/// Example:
/// "assets/archive.tar.gz" returns ".gz".
constexpr std::string_view
GetExtension(std::string_view path) noexcept
{
    const std::string_view fileName = GetFileName(path);

    if(fileName == "." || fileName == "..")
    {
        return {};
    }

    const std::size_t dot = fileName.find_last_of('.');

    if(dot == std::string_view::npos || dot == 0)
    {
        return {};
    }

    return fileName.substr(dot);
}

/// Returns the filename without its final extension.
/// Directory components are not included.
///
/// Example:
/// "assets/archive.tar.gz" returns "archive.tar".
constexpr std::string_view
GetStem(std::string_view path) noexcept
{
    const std::string_view fileName = GetFileName(path);
    const std::string_view extension = GetExtension(fileName);

    return fileName.substr(0, fileName.size() - extension.size());
}
} // namespace FilePathHelper