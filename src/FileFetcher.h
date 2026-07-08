#pragma once

#include "Result.h"

#include <span>
#include <string>
#include <vector>

namespace mlg::detail
{
struct FileFetcherImpl;
}

class FileFetcher final
{
public:

    ~FileFetcher();
    FileFetcher(const FileFetcher&) = delete;
    FileFetcher& operator=(const FileFetcher&) = delete;
    FileFetcher(FileFetcher&& other) noexcept;
    FileFetcher& operator=(FileFetcher&& other) noexcept;

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
        Request(Request&& other) noexcept;
        Request& operator=(Request&& other) noexcept;

        bool IsPending() const { return m_Status == RequestStatus::Pending; }
        bool Succeeded() const { return m_Status == RequestStatus::Success; }

        std::span<const uint8_t> GetData() const;

        void MoveDataTo(std::vector<uint8_t>& outBuffer);

        const std::string& GetFilePath() const { return m_FilePath; }

    private:

        friend class FileFetcher;

        void SetComplete(RequestStatus status);

        struct SDL_AsyncIO* m_AsyncIO{nullptr};

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

    explicit FileFetcher(mlg::detail::FileFetcherImpl* impl);

    Result<> IssueRead(Request& request);

    mlg::detail::FileFetcherImpl* m_Impl{ nullptr };
};