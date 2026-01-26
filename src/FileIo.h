#pragma once

#include <cstddef>
#include <string>

/// @brief Provides asynchronous file input/output operations.
class FileIo final
{
public:

    /// @brief Represents the result of a file read operation.
    /// Passed to the read callback.
    class ReadResult
    {
        friend class FileIo;
    public:

        bool Succeeded() const { return m_Succeeded; }

        const std::string& Error() const { return m_Error; }

        const uint8_t* Data() const { return m_Data; }

        size_t BytesRead() const { return m_BytesRead; }

    private:
        ReadResult(const uint8_t* data, const size_t bytesRead)
            : m_Data(data),
              m_BytesRead(bytesRead),
              m_Succeeded(true)
        {
        }

        explicit ReadResult(std::string_view error)
            : m_Error(error),
              m_Succeeded(false)
        {
        }

        explicit ReadResult(std::string&& error) noexcept
            : m_Error(std::move(error)),
              m_Succeeded(false)
        {
        }
        const uint8_t* m_Data{ nullptr };

        size_t m_BytesRead{ 0 };
        std::string m_Error;

        bool m_Succeeded{ false };
    };

    using ReadCallback = void (*)(const ReadResult& result, void* userData);

    FileIo() = delete;

    // Startup, Shutdown(), and ProcessEvents() must be called from the main thread.

    /// @brief  Initialize the FileIO system.
    static bool Startup();

    /// @brief Shutdown the FileIO system.
    static void Shutdown();

    /// @brief Process completed file I/O operations.
    static void ProcessEvents();

    /// @brief Queue a file read operation.
    static void QueueRead(std::string_view path, ReadCallback callback, void* userData);

private:

    /// @brief Platform-specific startup operations.
    static bool PlatformStartup();

    /// @brief Platform-specific shutdown operations.
    static bool PlatformShutdown();
};
