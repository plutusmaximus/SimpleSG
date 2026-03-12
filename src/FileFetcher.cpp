#include "FileFetcher.h"

#include "Log.h"

static HANDLE s_IOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);

Result<>
FileFetcher::Fetch(FileFetcher::Request& request)
{
    request.m_hFile = ::CreateFileA(request.FilePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if(request.m_hFile == INVALID_HANDLE_VALUE)
    {
        Log::Error("Failed to open file: {}", request.FilePath);
        request.SetComplete(Failure);
        return Result<>::Fail;
    }

    if(request.BytesRequested == 0)
    {
        auto result = GetFileSize(request);
        if(!result)
        {
            Log::Error("Failed to get file size: {}", request.FilePath);
            request.SetComplete(Failure);
            return Result<>::Fail;
        }
        request.BytesRequested = *result;
    }

    if(request.Data.size() < request.BytesRequested)
    {
        request.Data.resize(request.BytesRequested);
    }

    const ULONG_PTR completionKey = reinterpret_cast<ULONG_PTR>(&request);
    if(nullptr == ::CreateIoCompletionPort(request.m_hFile, s_IOCP, completionKey, 0))
    {
        Log::Error("Failed to bind file to IOCP: {}, error: {}", request.FilePath, ::GetLastError());
        request.SetComplete(Failure);
        return Result<>::Fail;
    }

    if(!IssueRead(request))
    {
        Log::Error("Failed to issue read for file: {}", request.FilePath);
        request.SetComplete(Failure);
        return Result<>::Fail;
    }

    request.m_Status = Pending;

    return Result<>::Ok;
}

Result<>
FileFetcher::ProcessCompletions()
{
    BOOL ok;
    std::array<OVERLAPPED_ENTRY, 8> entries = {};
    ULONG numEntriesRemoved = 0;
    {
        ok = ::GetQueuedCompletionStatusEx(s_IOCP,
            entries.data(),
            static_cast<ULONG>(entries.size()),
            &numEntriesRemoved,
            0,
            FALSE);
    }

    if(!ok)
    {
        const DWORD err = ::GetLastError();

        if(WAIT_TIMEOUT == err)
        {
            // No completions available
            return Result<>::Ok;
        }

        if(ERROR_ABANDONED_WAIT_0 == err)
        {
            // IOCP was closed during shutdown.
            return Result<>::Ok;
        }

        // Some other error occurred - assume it's fatal.
        Log::Error("GetQueuedCompletionStatusEx failed, error: {}", err);

        return Result<>::Ok;
    }

    // If we get here, at least one read completed successfully.

    for(ULONG i = 0; i < numEntriesRemoved; ++i)
    {
        OVERLAPPED_ENTRY& entry = entries[i];
        Request* req = reinterpret_cast<Request*>(entry.lpCompletionKey);

        if(!req)
        {
            continue;
        }

        req->BytesRead += entry.dwNumberOfBytesTransferred;

        // Attempt to read more bytes.  This could complete immediately.
        IssueRead(*req);
    }

    return Result<>::Ok;
}

Result<size_t>
FileFetcher::GetFileSize(const FileFetcher::Request& request)
{
    static_assert(sizeof(size_t) >= sizeof(LARGE_INTEGER::QuadPart), "size_t is too small to hold file size");
    LARGE_INTEGER size;
    MLG_CHECK(GetFileSizeEx(request.m_hFile, &size),
        "Failed to open file: {}, error: {}",
        request.m_hFile,
        ::GetLastError());

    return static_cast<size_t>(size.QuadPart);
}

Result<>
FileFetcher::IssueRead(FileFetcher::Request& req)
{
    bool done = false;
    while(req.BytesRead < req.BytesRequested && !done)
    {
        LARGE_INTEGER li;
        li.QuadPart = req.BytesRead;

        // Set up the offset into the file from which to read.
        req.m_Ov.Offset = li.LowPart;
        req.m_Ov.OffsetHigh = li.HighPart;

        const size_t bytesRemaining = req.BytesRequested - req.BytesRead;

        DWORD bytesRead = 0;

        // Re-issue read for remaining bytes
        const BOOL ok = ::ReadFile(req.m_hFile,
            req.Data.data() + req.BytesRead,
            static_cast<DWORD>(bytesRemaining),
            &bytesRead,
            &req.m_Ov);

        if(ok)
        {
            // Request completed synchronously - loop again if necessary
            req.BytesRead += bytesRead;
            continue;
        }

        const DWORD err = ::GetLastError();

        if(err == ERROR_IO_PENDING)
        {
            // Still pending, break out of the loop.
            done = true;
            continue;
        }

        Log::Error("Failed to issue read for file: {}, error: {}", req.FilePath, err);

        req.SetComplete(Failure);

        return {};
    }

    if(req.IsPending() && req.BytesRead >= req.BytesRequested)
    {
        req.SetComplete(Success);
    }

    return Result<>::Ok;
}
