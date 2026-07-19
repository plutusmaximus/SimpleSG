#pragma once

#include "foreign_ptr.h"
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
    FileFetcher(FileFetcher&&) = delete;
    FileFetcher& operator=(FileFetcher&&) = delete;

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

        // Use a foreign_ptr to make Request easily movable.
        // Note that foreign_ptr does not destroy the pointer, so we must call SDL_CloseAsyncIO() or
        // SDL_AbortAsyncIO() to clean up the SDL_AsyncIO object.  We do this in
        // FileFetcher::ProcessCompletions() when the request is complete.
        foreign_ptr<SDL_AsyncIO> m_AsyncIO{ nullptr };

        std::string m_FilePath;
        size_t m_BytesRequested{ 0 };
        size_t m_BytesRead{ 0 };
        std::vector<uint8_t> m_Data;

        RequestStatus m_Status{ RequestStatus::None };
    };

    static Result<std::unique_ptr<FileFetcher>> Create();

    Result<> Fetch(Request& request);

    void ProcessCompletions();

private:
    explicit FileFetcher(SDL_AsyncIOQueue* ioQueue)
        : m_IoQueue(ioQueue)
    {
    }

    static Result<size_t> GetFileSize(const Request& request);

    Result<> IssueRead(Request& request);

    SDL_AsyncIOQueue* m_IoQueue{ nullptr };
};