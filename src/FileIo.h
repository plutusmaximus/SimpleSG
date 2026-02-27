#pragma once

#include "imstring.h"
#include "Result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

/// @brief Provides asynchronous file I/O operations.
class FileIo final
{
    friend class ReadRequest;

public:
    /// @brief Abstract base class representing fetched file data for a specific platform.
    class PlatformFetchData
    {
    public:
        virtual ~PlatformFetchData() = 0;

        const std::span<const uint8_t> Bytes;

    protected:
        PlatformFetchData(const uint8_t* bytes, const size_t bytesRead)
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

    class ReadRequest
    {
    public:
        explicit ReadRequest(const imstring& path)
            : Path(path)
        {
        }

        ReadRequest(const ReadRequest&) = delete;
        ReadRequest& operator=(const ReadRequest&) = delete;
        ReadRequest(ReadRequest&&) = delete;
        ReadRequest& operator=(ReadRequest&&) = delete;

        virtual ~ReadRequest() = 0;

        void Link(ReadRequest* next);

        void Unlink();

        imstring Path;

        FileIo::AsyncToken Token = FileIo::AsyncToken::NewToken();

        std::optional<Error> Error;

        ReadRequest* m_Next{ nullptr };
        ReadRequest* m_Prev{ nullptr };
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

    class FetchData
    {
        friend class FileIo;
    public:

        const uint8_t* data() const { return m_DataPtr ? m_DataPtr->Bytes.data() : nullptr; }
        size_t size() const { return m_DataPtr ? m_DataPtr->Bytes.size() : 0; }

        FetchData() = default;
        FetchData(const FetchData&) = delete;
        FetchData& operator=(const FetchData&) = delete;
        FetchData(FetchData&&) = default;
        FetchData& operator=(FetchData&&) = default;

    private:

        explicit FetchData(std::unique_ptr<PlatformFetchData>&& dataPtr)
            : m_DataPtr(std::move(dataPtr))
        {
        }

        std::unique_ptr<PlatformFetchData> m_DataPtr;
    };

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
    static Result<FetchData> GetResult(const AsyncToken token);

private:

    /// @brief Platform-specific startup operations.
    static bool PlatformStartup();

    /// @brief Platform-specific shutdown operations.
    static bool PlatformShutdown();

    /// @brief Processes completed fetch operations.
    static void ProcessCompletions();

    static void CompleteRequestSuccess(class ReadRequest* request, const size_t bytesRead);

    static void CompleteRequestFailure(class ReadRequest* request, const Error& error);

    static Result<FileIo::FetchData> GetResultImpl(FileIo::ReadRequest* req);
};
