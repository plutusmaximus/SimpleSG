#pragma once

#include "Result.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

struct SDL_AsyncIO;
struct SDL_AsyncIOQueue;

/// @brief A simple file fetcher that uses SDL's Async IO to read files asynchronously.
/// Do not use simultaneously from multiple threads.  SDL's Async IO is thread-safe, but this class
/// is not.
class FileFetcher final
{
public:
    ~FileFetcher();
    FileFetcher(const FileFetcher&) = delete;
    FileFetcher& operator=(const FileFetcher&) = delete;
    FileFetcher(FileFetcher&&) = default;
    FileFetcher& operator=(FileFetcher&&) = default;

    enum class RequestStatus : uint8_t
    {
        None,
        Failure,
        Pending,
        Success,
    };

    class Request
    {
    public:
        explicit Request(std::string filePath);
        ~Request();
        Request(const Request&) = delete;
        Request& operator=(const Request&) = delete;
        Request(Request&& other) = default;
        Request& operator=(Request&& other) = default;

        bool IsPending() const { return m_Status == RequestStatus::Pending; }
        bool Succeeded() const { return m_Status == RequestStatus::Success; }

        std::span<const uint8_t> GetData() const;

        void MoveDataTo(std::vector<uint8_t>& outBuffer);

        const std::string& GetFilePath() const { return m_FilePath; }

    private:
        friend class FileFetcher;

        void SetComplete(RequestStatus status);

        static void Deleter(SDL_AsyncIO*) {}

        // Use a unique_ptr to make Request easily movable.
        // Use a custom deleter that does nothing - SDL_AsyncIO is disposed by calling
        // SDL_CloseAsyncIO() when the request is complete.
        std::unique_ptr<SDL_AsyncIO, decltype(&Deleter)> m_AsyncIO{ nullptr, &Deleter };

        std::string m_FilePath;
        size_t m_BytesRequested{ 0 };
        size_t m_BytesRead{ 0 };
        std::vector<uint8_t> m_Data;

        RequestStatus m_Status{ RequestStatus::None };
    };

    static Result<std::unique_ptr<FileFetcher>> Create();

    Result<> Fetch(Request& request);

    Result<> ProcessCompletions();

private:
    // Custom deleter for SDL_AsyncIOQueue.  SDL_AsyncIOQueue is disposed by calling
    // SDL_CloseAsyncIO() when the FileFetcher is destroyed.
    static void Deleter(SDL_AsyncIOQueue* asyncIO);

    // Use a unique_ptr to make FileFetcher easily movable.
    // Construct it with the custom deleter that knows how to dispose of the SDL_AsyncIOQueue.
    using DeleterType = decltype(&Deleter);
    using UniquePtrType = std::unique_ptr<SDL_AsyncIOQueue, DeleterType>;

    explicit FileFetcher(UniquePtrType impl)
        : m_IoQueue(std::move(impl))
    {
    }

    static Result<size_t> GetFileSize(const Request& request);

    Result<> IssueRead(Request& request);

    UniquePtrType m_IoQueue{ nullptr, &Deleter };
};