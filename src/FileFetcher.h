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

    FileFetcher();
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

        struct SDL_AsyncIO* m_AsyncIO{nullptr};

        std::string m_FilePath;
        size_t m_BytesRequested{0};
        size_t m_BytesRead{0};
        std::vector<uint8_t> m_Data;

        RequestStatus m_Status{RequestStatus::None};
    };

    Result<> Fetch(Request& request);

    Result<> ProcessCompletions();

private:

    static Result<size_t> GetFileSize(const Request& request);

    Result<> IssueRead(Request& request);

    static constexpr size_t kSizeofImplStorage = 72;

    alignas(std::max_align_t) uint8_t m_ImplStorage[kSizeofImplStorage]{};
    
    mlg::detail::FileFetcherImpl* m_Impl{ static_cast<mlg::detail::FileFetcherImpl*>(
        static_cast<void*>(m_ImplStorage)) }; // NOLINT(bugprone-casting-through-void)
};