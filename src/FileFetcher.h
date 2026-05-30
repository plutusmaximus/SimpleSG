#pragma once

#include "Result.h"

#include <span>
#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

class FileFetcher
{
public:

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

        explicit Request(std::string filePath)
            : m_FilePath(std::move(filePath))
        {
        }

        Request(const Request&) = delete;
        Request& operator=(const Request&) = delete;
        Request(Request&& other) = delete;
        Request& operator=(Request&& other) = delete;

        ~Request()
        {
            if(IsPending())
            {
                SetComplete(RequestStatus::Failure);
            }
        }

        bool IsPending() const { return m_Status == RequestStatus::Pending; }
        bool Succeeded() const { return m_Status == RequestStatus::Success; }

        std::span<const uint8_t> GetData() const
        {
            MLG_ASSERT(Succeeded(), "Attempted to access data of a request that did not succeed or is still pending");
             return m_Data;
        }

        void MoveDataTo(std::vector<uint8_t>& outBuffer)
        {
            MLG_ASSERT(Succeeded(), "Attempted to move data from a request that did not succeed or is still pending");
            outBuffer = std::move(m_Data);
        }

        const std::string& GetFilePath() const { return m_FilePath; }

    private:

        friend class FileFetcher;

        void SetComplete(RequestStatus status)
        {
            MLG_ASSERT(RequestStatus::Pending == m_Status,
                "Attempted to complete a request that is not pending");
            MLG_ASSERT(status == RequestStatus::Success || status == RequestStatus::Failure,
                "Invalid status for completion");

#if defined(_WIN32)
            if(m_hFile)
            {
                ::CancelIoEx(m_hFile, &m_Ov);
                ::CloseHandle(m_hFile);
                m_hFile = nullptr;
            }
#endif

            m_Status = status;
        }

#if defined(_WIN32)
        HANDLE m_hFile{nullptr};
        OVERLAPPED m_Ov{0};
#else
        int m_Fd{-1};
#endif
        std::string m_FilePath;
        size_t m_BytesRequested{0};
        size_t m_BytesRead{0};
        std::vector<uint8_t> m_Data;

        RequestStatus m_Status{RequestStatus::None};
    };

    static Result<> Fetch(Request& request);

    static Result<> ProcessCompletions();

private:

    static Result<size_t> GetFileSize(const Request& request);

    static Result<> IssueRead(Request& req);
};