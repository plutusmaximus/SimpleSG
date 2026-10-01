#include <string_view>
#define MLG_LOGGER_NAME "PATH"

#include "FilePath.h"

#include <utility>

namespace
{
bool
HasValidComponents(const std::string_view path)
{
    if(path.empty()
        || path.size() > RelativeFilePath::kMaxLength
        || path.front() == '/'
        || path.back() == '/'
        || path.find_first_of("\\:") != std::string_view::npos
        || path.contains('\0')
        || path.contains(".."))
    {
        return false;
    }

    size_t start = 0;
    while(start < path.size())
    {
        const size_t end = path.find('/', start);
        const std::string_view component = path.substr(start, end - start);
        if(component.empty() || component == ".")
        {
            return false;
        }

        if(end == std::string_view::npos)
        {
            return true;
        }
        start = end + 1;
    }

    return true;
}
} // namespace

/// RelativeFilePath

RelativeFilePath::RelativeFilePath(FixedString<kStorageSize> value)
    : m_Value(std::move(value))
{
}

Result<RelativeFilePath>
RelativeFilePath::Create(const std::string_view path)
{
    MLG_CHECKV(HasValidComponents(path), "Invalid file path: {}", path);

    return RelativeFilePath(FixedString<kStorageSize>(path));
}

/// DirectoryPath

DirectoryPath::DirectoryPath(FixedString<RelativeFilePath::kStorageSize> value)
    : m_Value(std::move(value))
{
}

DirectoryPath
DirectoryPath::Current()
{
    return DirectoryPath(FixedString<RelativeFilePath::kStorageSize>("./"));
}

Result<DirectoryPath>
DirectoryPath::Create(const std::string_view path)
{
    if(path == "./")
    {
        return Current();
    }

    MLG_CHECKV(!path.empty(), "Directory path must not be empty");

    if(path.back() == '/')
    {
        MLG_CHECKV(path.size() <= RelativeFilePath::kMaxLength, "Directory path is too long: {}", path);
        MLG_CHECKV(HasValidComponents(path.substr(0, path.size() - 1)),
            "Invalid directory path: {}",
            path);

        return DirectoryPath(FixedString<RelativeFilePath::kStorageSize>(path));
    }

    // +1 for the trailing '/'
    MLG_CHECKV(path.size() + 1 <= RelativeFilePath::kMaxLength, "Directory path is too long: {}", path);
    MLG_CHECKV(HasValidComponents(path), "Invalid directory path: {}", path);

    // Add the trailing '/' to the directory path
    auto fixedPath = FixedString<RelativeFilePath::kStorageSize>::TryCat({ path, "/" });
    MLG_CHECKV(fixedPath, "Failed to create directory path: {}", path);

    return DirectoryPath(std::move(*fixedPath));
}

Result<FilePath>
DirectoryPath::Join(const RelativeFilePath& file) const
{
    return ::Join(*this, file);
}

Result<FilePath>
DirectoryPath::Join(const std::string_view file) const
{
    auto relativeFile = RelativeFilePath::Create(file);
    MLG_CHECKV(relativeFile, "Failed to create relative file path: {}", file);
    return Join(*relativeFile);
}

/// FilePath

FilePath::FilePath(FixedString<kStorageSize> value)
    : m_Value(std::move(value))
{
}

Result<FilePath>
Join(const DirectoryPath& directory, const RelativeFilePath& file)
{
    const std::string_view directoryView = std::string_view(directory);
    const std::string_view fileView = std::string_view(file);

    if(directoryView == "./")
    {
        return FilePath(FixedString<FilePath::kStorageSize>(fileView));
    }

    MLG_CHECKV(directoryView.size() <= FilePath::kMaxLength - fileView.size(),
        "Joined file path is too long: {}{}",
        directoryView,
        fileView);

    auto joined = FixedString<FilePath::kStorageSize>::TryCat({ directoryView, fileView });
    MLG_CHECKV(joined, "Failed to join file path: {}{}", directoryView, fileView);

    return FilePath(std::move(*joined));
}
