#pragma once

#include "Result.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct SDL_AsyncIO;
struct SDL_AsyncIOQueue;

using FetchRequestId = uint64_t;

/// @brief A simple file fetcher that uses SDL's Async IO to read files asynchronously.
/// Do not use simultaneously from multiple threads.  SDL's Async IO is thread-safe, but this class
/// is not.
class FileFetcher final
{
public:
    ~FileFetcher();
    FileFetcher(const FileFetcher&) = delete;
    FileFetcher& operator=(const FileFetcher&) = delete;
    FileFetcher(FileFetcher&&) = delete;
    FileFetcher& operator=(FileFetcher&&) = delete;

    /// @brief Creates a new instance of the FileFetcher.
    static Result<std::unique_ptr<FileFetcher>> Create();

    /// @brief Initiates an asynchronous fetch for the specified file.
    /// Returns a FetchRequestId that can be used to track the request.
    Result<FetchRequestId> Fetch(std::string filePath);

    /// @brief Checks if the specified fetch request is still pending.
    bool IsPending(const FetchRequestId requestId) const;

    /// @brief Retrieves the data for the specified fetch request once it has completed.
    /// If the request has not completed successfully, this will return a failure result.
    /// If the request has completed successfully, the data will be moved into the provided output buffer.
    Result<> Take(const FetchRequestId requestId, std::vector<uint8_t>& outBuffer);

    /// @brief Processes pending asynchronous IO operations.  Must be called once per frame.
    void ProcessCompletions();

private:

    /// The maximum number of read attempts before failing a request.
    static constexpr uint32_t kMaxReadAttempts = 5;

    class Request
    {
    public:
        enum class Status : uint8_t
        {
            None,
            Failure,
            Pending,
            Success,
        };
        Request() = default;
        ~Request();
        Request(const Request&) = delete;
        Request& operator=(const Request&) = delete;
        Request(Request&&) = delete;
        Request& operator=(Request&&) = delete;

        bool IsPending() const { return m_Status == Status::Pending; }
        bool Succeeded() const { return m_Status == Status::Success; }

        SDL_AsyncIO* m_AsyncIO{ nullptr };

        std::string m_FilePath;
        size_t m_BytesRequested{ 0 };
        size_t m_BytesRead{ 0 };
        std::vector<uint8_t> m_Data;
        uint32_t m_ReadAttempts{ 0 };

        Status m_Status{ Status::None };

        FetchRequestId m_RequestId{ 0 };
    };

    static constexpr uint32_t kInvalidIndex = UINT32_MAX;
    static constexpr uint32_t kInvalidGeneration = UINT32_MAX;

    /// Storage for a fetch request.
    struct RequestBuffer
    {
        Request* m_Request{ nullptr };
        RequestBuffer* m_Next{ nullptr };

        /// The index of this request buffer within the heap.
        uint32_t m_Index{ kInvalidIndex };

        /// The generation of this request buffer, used to detect stale requests.
        uint32_t m_Generation{ kInvalidGeneration };

        /// Storage for the Request object.
        alignas(Request) char m_Storage[sizeof(Request)]{};
    };

    explicit FileFetcher(SDL_AsyncIOQueue* ioQueue)
        : m_IoQueue(ioQueue)
    {
    }

    Result<> IssueRead(Request& request);

    RequestBuffer* AllocateRequest();

    void FreeRequest(RequestBuffer* requestBuf);

    void SetSucceeded(const FetchRequestId requestId);

    void SetFailed(const FetchRequestId requestId);

    void Close(const FetchRequestId requestId);

    RequestBuffer* GetRequestBuffer(const FetchRequestId requestId);

    const RequestBuffer* GetRequestBuffer(const FetchRequestId requestId) const;

    SDL_AsyncIOQueue* m_IoQueue{ nullptr };

    static constexpr size_t kBucketSize = 256;

    uint32_t m_HeapSize{ 0 };
    uint32_t m_AllocCount{ 0 };
    std::vector<std::vector<RequestBuffer>> m_RequestBuffers;
    RequestBuffer* m_FreeList{ nullptr };
};