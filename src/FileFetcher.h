#pragma once

#include "FilePath.h"
#include "Result.h"

#include <cstdint>
#include <vector>

struct SDL_AsyncIO;
struct SDL_AsyncIOQueue;

/// A unique ID representing a FileFetcher request.
class FetchRequestId
{
public:
    constexpr FetchRequestId() = default;

    friend bool operator==(const FetchRequestId& lhs, const FetchRequestId& rhs) = default;

private:
    friend class FileFetcher;

    FetchRequestId GetNextGeneration() const
    {
        FetchRequestId next = *this;
        ++next.m_Generation;
        if(0 == next.m_Generation)
        {
            ++next.m_Generation;
        }

        return next;
    }

    uint32_t m_Index{ 0 };
    uint32_t m_Generation{ 0 };
};

/// A simple file fetcher that uses SDL's Async IO to read files asynchronously.
/// Do not use simultaneously from multiple threads.  SDL's Async IO is thread-safe, but this class
/// is not.
class FileFetcher final
{
public:
    FileFetcher() = default;
    ~FileFetcher();
    FileFetcher(const FileFetcher&) = delete;
    FileFetcher& operator=(const FileFetcher&) = delete;
    FileFetcher(FileFetcher&&) = delete;
    FileFetcher& operator=(FileFetcher&&) = delete;

    /// Initiates an asynchronous fetch for the specified file.
    /// Returns a FetchRequestId that can be used to track the request.
    Result<FetchRequestId> Fetch(const FilePath& filePath);

    /// Checks if the fetch request is still pending.
    bool IsPending(const FetchRequestId requestId) const;

    /// Retrieves the data for the fetch request once it has completed.
    /// If the request is still pending, this will return a failure result.
    /// If the request completed but failed to fetch the file, this will return a failure
    /// result. If the request completed successfully, the data will be returned.
    Result<std::vector<uint8_t>> Take(const FetchRequestId requestId);

    /// Processes pending asynchronous IO operations.  Must be called at least once per frame.
    void ProcessCompletions();

private:
    /// The maximum number of read attempts before failing a request.
    static constexpr uint32_t kMaxReadAttempts = 5;

    class Request
    {
    public:
        enum class Stage
        {
            None,
            Pending,
            Succeeded,
            Failed,
        };

        explicit Request(const RelativeFilePath& relativePath)
            : m_DiagFilePath(relativePath)
        {
        }
        ~Request();
        Request(const Request&) = delete;
        Request& operator=(const Request&) = delete;
        Request(Request&&) = delete;
        Request& operator=(Request&&) = delete;

        bool IsPending() const { return m_Stage == Stage::Pending; }
        bool Succeeded() const { return m_Stage == Stage::Succeeded; }

        SDL_AsyncIO* m_AsyncIO{ nullptr };

        RelativeFilePath m_DiagFilePath; // Used only for logging
        size_t m_BytesRequested{ 0 };
        size_t m_BytesRead{ 0 };
        std::vector<uint8_t> m_Data;
        uint32_t m_ReadAttempts{ 0 };

        Stage m_Stage{ Stage::None };
    };

    /// Storage for a fetch request.
    struct RequestWrapper
    {
        Request* m_Request{ nullptr };
        RequestWrapper* m_Next{ nullptr };

        FetchRequestId m_RequestId;

        /// Storage for the Request object.
        /// Structuring like this enables construction/destruction of the Request object in-place.
        alignas(Request) char m_Storage[sizeof(Request)]{};
    };

    explicit FileFetcher(SDL_AsyncIOQueue* ioQueue)
        : m_IoQueue(ioQueue)
    {
    }

    /// Ensures the SDL IO queue is properly initialized.
    Result<> EnsureIoQueue();

    /// Issues a read request.  Multiple reads can be issued for the
    /// same request until either the entire file has been read, a failure occurs,
    /// or the maximum number of tries is reached.
    Result<> IssueRead(RequestWrapper& wrapper);

    Result<RequestWrapper*> AllocateRequest(const FilePath& filePath);

    void FreeRequest(RequestWrapper* wrapper);

    void SetSucceeded(const FetchRequestId requestId);

    void SetFailed(const FetchRequestId requestId);

    void Close(const FetchRequestId requestId);

    RequestWrapper* GetRequest(const FetchRequestId requestId);

    const RequestWrapper* GetRequest(const FetchRequestId requestId) const;

    SDL_AsyncIOQueue* m_IoQueue{ nullptr };

    uint32_t m_HeapSize{ 0 };
    uint32_t m_AllocCount{ 0 };
    std::vector<std::vector<RequestWrapper>> m_RequestPool;
    RequestWrapper* m_FreeList{ nullptr };
};