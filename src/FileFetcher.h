#pragma once

#include "Result.h"

#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mlg::detail
{
struct FileFetcherImpl;
}

struct SDL_AsyncIO;

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
        // Use a custom deleter that does nothing - SDL_AsyncIO is disposed by calling SDL_CloseAsyncIO() when the request is complete.
        std::unique_ptr<SDL_AsyncIO, decltype(&Deleter)> m_AsyncIO{ nullptr, &Deleter };

        std::string m_FilePath;
        size_t m_BytesRequested{0};
        size_t m_BytesRead{0};
        std::vector<uint8_t> m_Data;

        RequestStatus m_Status{RequestStatus::None};
    };

    static Result<FileFetcher> Create();

    Result<> Fetch(Request& request);

    Result<> ProcessCompletions();

private:

    static Result<size_t> GetFileSize(const Request& request);

    FileFetcher() = default;

    static void DeleteImpl(mlg::detail::FileFetcherImpl* impl);

    Result<> IssueRead(Request& request);

    std::unique_ptr<mlg::detail::FileFetcherImpl, decltype(&DeleteImpl)> m_Impl{ nullptr, &DeleteImpl };
};