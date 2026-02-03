#pragma once

#include "Error.h"
#include "imstring.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

/// @brief Provides asynchronous file I/O operations.
class FileIo final
{
    friend class ReadRequest;

public:
    /// @brief Abstract base class representing fetched file data.
    class FetchData
    {
    public:
        virtual ~FetchData() = 0;

        const std::span<const uint8_t> Bytes;

    protected:
        FetchData(const uint8_t* bytes, const size_t bytesRead)
            : Bytes(bytes, bytesRead)
        {
        }
    };

    /// @brief Token representing the status of a file I/O operation.
    /// Used to query the status or retrieve results of asynchronous operations.
    class AsyncToken
    {
        using ValueType = uint32_t;

    public:
        AsyncToken() = default;

        bool operator==(const AsyncToken& other) const { return m_Value == other.m_Value; }
        bool operator!=(const AsyncToken& other) const { return m_Value != other.m_Value; }

        static AsyncToken NewToken();

    private:
        inline static constexpr ValueType InvalidValue{ 0 };

        explicit constexpr AsyncToken(ValueType value)
            : m_Value(value)
        {
        }

        ValueType m_Value{ 0 };
    };

    /// @brief Enumeration representing the status of a fetch operation.
    enum FetchStatus
    {
        /// @brief The fetch operation has not started or the token is invalid.
        None,
        /// @brief The fetch operation is still in progress.
        Pending,
        /// @brief The fetch operation has completed (either successfully or with an error).
        Completed,
    };

    /// @brief Smart pointer type for fetched file data.
    using FetchDataPtr = std::unique_ptr<FetchData>;

    FileIo() = delete;

    /// @brief  Initialize the FileIO system.
    static bool Startup();

    /// @brief Shutdown the FileIO system.
    static void Shutdown();

    /// @brief Initiates an asynchronous file fetch operation.
    static Result<AsyncToken> Fetch(const imstring& path);

    /// @brief Gets the current status of a fetch operation.
    static FetchStatus GetStatus(const AsyncToken token);

    /// @brief Checks if a fetch operation is still pending.
    static bool IsPending(const AsyncToken token)
    {
        return GetStatus(token) == FetchStatus::Pending;
    }

    /// @brief Retrieves the result of a completed fetch operation.
    /// Returns an error if the operation failed, or the operation is not complete.
    static Result<FetchDataPtr> GetResult(const AsyncToken token);

private:
    /// @brief Platform-specific startup operations.
    static bool PlatformStartup();

    /// @brief Platform-specific shutdown operations.
    static bool PlatformShutdown();

    /// @brief Processes completed fetch operations.
    static void ProcessCompletions();

    static void CompleteRequestSuccess(class ReadRequest* request, const size_t bytesRead);

    static void CompleteRequestFailure(class ReadRequest* request, const Error& error);
};
