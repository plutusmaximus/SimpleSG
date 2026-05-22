#pragma once

#include <Result.h>

#include <string>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

class FileFetcher
{
public:

    enum RequestStatus
    {
        None,
        Failure,
        Pending,
        Success,
    };

    class Request
    {
    public:

        explicit Request(const std::string& filePath)
            : FilePath(filePath)
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
                SetComplete(Failure);
            }
        }

        bool IsPending() const { return m_Status == Pending; }
        bool Succeeded() const { return m_Status == Success; }

        std::string FilePath;
        std::vector<uint8_t> Data;
        size_t BytesRequested{0};
        size_t BytesRead{0};

    private:

        friend class FileFetcher;

        void SetComplete(RequestStatus status)
        {
#if !defined(_WIN32)
            MLG_ASSERT(false, "FileFetcher is not implemented on this platform");
#endif
            if(!IsPending())
            {
                return;
            }

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
#endif
        RequestStatus m_Status{None};
    };

    static Result<> Fetch(Request& request);

    static Result<> ProcessCompletions();

private:

    static Result<size_t> GetFileSize(const Request& request);

    static Result<> IssueRead(Request& req);
};