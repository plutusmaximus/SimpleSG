#include "FileFetcher.h"

#include "Log.h"

#if defined(_WIN32) && WIN32_USE_IOCP

namespace
{
HANDLE& IOCP()
{
    static HANDLE s_IOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    return s_IOCP;
}
}  // namespace

Result<>
FileFetcher::Fetch(FileFetcher::Request& request)
{
    MLG_CHECKV(RequestStatus::None == request.m_Status, "Request is already in progress or completed");

    request.m_Status = RequestStatus::Pending;

    request.m_hFile = ::CreateFileA(request.m_FilePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if(request.m_hFile == INVALID_HANDLE_VALUE)
    {
        MLG_ERROR("Failed to open file: {}", request.m_FilePath);
        request.SetComplete(RequestStatus::Failure);
        return Result<>::Fail;
    }

    if(request.m_BytesRequested == 0)
    {
        auto result = GetFileSize(request);
        if(!result)
        {
            MLG_ERROR("Failed to get file size: {}", request.m_FilePath);
            request.SetComplete(RequestStatus::Failure);
            return Result<>::Fail;
        }
        request.m_BytesRequested = *result;
    }

    if(request.m_Data.size() < request.m_BytesRequested)
    {
        request.m_Data.resize(request.m_BytesRequested);
    }

    const ULONG_PTR completionKey = reinterpret_cast<ULONG_PTR>(&request);
    if(nullptr == ::CreateIoCompletionPort(request.m_hFile, IOCP(), completionKey, 0))
    {
        MLG_ERROR("Failed to bind file to IOCP: {}, error: {}", request.m_FilePath, ::GetLastError());
        request.SetComplete(RequestStatus::Failure);
        return Result<>::Fail;
    }

    if(!IssueRead(request))
    {
        MLG_ERROR("Failed to issue read for file: {}", request.m_FilePath);
        request.SetComplete(RequestStatus::Failure);
        return Result<>::Fail;
    }

    return Result<>::Ok;
}

Result<>
FileFetcher::ProcessCompletions()
{
    BOOL ok;
    OVERLAPPED_ENTRY entries[8] = {};
    ULONG numEntriesRemoved = 0;
    {
        ok = ::GetQueuedCompletionStatusEx(IOCP(),
            entries,
            static_cast<ULONG>(std::size(entries)),
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
        MLG_ERROR("GetQueuedCompletionStatusEx failed, error: {}", err);

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

        req->m_BytesRead += entry.dwNumberOfBytesTransferred;

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
        request.m_FilePath,
        ::GetLastError());

    return static_cast<size_t>(size.QuadPart);
}

Result<>
FileFetcher::IssueRead(FileFetcher::Request& req)
{
    bool done = false;
    while(req.m_BytesRead < req.m_BytesRequested && !done)
    {
        LARGE_INTEGER li;
        li.QuadPart = req.m_BytesRead;

        // Set up the offset into the file from which to read.
        req.m_Ov.Offset = li.LowPart;
        req.m_Ov.OffsetHigh = li.HighPart;

        const size_t bytesRemaining = req.m_BytesRequested - req.m_BytesRead;

        DWORD bytesRead = 0;

        // Re-issue read for remaining bytes
        const BOOL ok = ::ReadFile(req.m_hFile,
            req.m_Data.data() + req.m_BytesRead,
            static_cast<DWORD>(bytesRemaining),
            &bytesRead,
            &req.m_Ov);

        if(ok)
        {
            // Request completed synchronously - loop again if necessary
            req.m_BytesRead += bytesRead;
            continue;
        }

        const DWORD err = ::GetLastError();

        if(err == ERROR_IO_PENDING)
        {
            // Still pending, break out of the loop.
            done = true;
            continue;
        }

        MLG_ERROR("Failed to issue read for file: {}, error: {}", req.m_FilePath, err);

        req.SetComplete(RequestStatus::Failure);

        return Result<>::Fail;
    }

    if(req.IsPending() && req.m_BytesRead >= req.m_BytesRequested)
    {
        req.SetComplete(RequestStatus::Success);
    }

    return Result<>::Ok;
}

#else  // !_WIN32

#include  "scope_exit.h"

#include <mutex>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_asyncio.h>

namespace
{

struct FFGlobals
{
    static inline std::mutex Mutex;
    static inline SDL_AsyncIOQueue* AsyncIOQueue{nullptr};

    static Result<> VerifyStarted()
    {
        MLG_CHECKV(AsyncIOQueue, "FileFetcher not initialized - call Startup()");

        return Result<>::Ok;
    }

    static inline auto OnShutdown = scope_exit(
        []()
        {
            MLG_ASSERT(nullptr == FFGlobals::AsyncIOQueue, "Async IO Queue not properly shut down");
        });
};
}  // namespace

Result<>
FileFetcher::Startup()
{
        const std::lock_guard lock(FFGlobals::Mutex);
        if(!FFGlobals::AsyncIOQueue)
        {
            FFGlobals::AsyncIOQueue = SDL_CreateAsyncIOQueue();
            MLG_CHECK(FFGlobals::AsyncIOQueue, "Failed to create SDL Async IO Queue: {}", SDL_GetError());
        }
        return Result<>::Ok;
}

void
FileFetcher::Shutdown()
{
    const std::lock_guard lock(FFGlobals::Mutex);
    if(FFGlobals::AsyncIOQueue)
    {
        FileFetcher::ProcessCompletions();
        SDL_SignalAsyncIOQueue(FFGlobals::AsyncIOQueue);
        SDL_DestroyAsyncIOQueue(FFGlobals::AsyncIOQueue);
        FFGlobals::AsyncIOQueue = nullptr;
    }
}

Result<>
FileFetcher::Fetch(FileFetcher::Request& request)
{
    MLG_CHECKV(RequestStatus::None == request.m_Status, "Request is already in progress or completed");
    MLG_CHECK(FFGlobals::VerifyStarted());

    request.m_Status = RequestStatus::Pending;

    defer_as(setFailedOnExit)
    {
        if(request.IsPending())
        {
            request.SetComplete(RequestStatus::Failure);
        }
    };

    request.m_AsyncIO = SDL_AsyncIOFromFile(request.m_FilePath.c_str(), "r");
    MLG_CHECK(request.m_AsyncIO,
        "Failed to create SDL Async IO for file: {}, error: {}",
        request.m_FilePath,
        SDL_GetError());

    defer_as(closeOnFailure)
    {
        SDL_CloseAsyncIO(request.m_AsyncIO, false, FFGlobals::AsyncIOQueue, &request);
        request.m_AsyncIO = nullptr;
    };

    const auto fileSize = GetFileSize(request);
    MLG_CHECK(fileSize);

    if(request.m_BytesRequested == 0)
    {
        request.m_BytesRequested = *fileSize;
    }
    else
    {
        MLG_CHECK(request.m_BytesRequested <= *fileSize,
            "Requested byte count {} exceeds file size {} for file: {}",
            request.m_BytesRequested,
            *fileSize,
            request.m_FilePath);
    }

    if(request.m_Data.size() < request.m_BytesRequested)
    {
        request.m_Data.resize(request.m_BytesRequested);
    }

    MLG_CHECK(IssueRead(request));

    closeOnFailure.release();
    setFailedOnExit.release();

    return Result<>::Ok;
}

Result<>
FileFetcher::ProcessCompletions()
{
    MLG_CHECK(FFGlobals::VerifyStarted());

    SDL_AsyncIOOutcome outcome;
    while(SDL_GetAsyncIOResult(FFGlobals::AsyncIOQueue, &outcome))
    {
        if(outcome.type == SDL_ASYNCIO_TASK_CLOSE)
        {
            continue;
        }

        if(outcome.type == SDL_ASYNCIO_TASK_READ)
        {
            Request* request = static_cast<Request*>(outcome.userdata);
            MLG_CHECK(request, "Received SDL Async IO completion with null userdata");

            switch(outcome.result)
            {
                case SDL_ASYNCIO_COMPLETE:
                    request->m_BytesRead += static_cast<size_t>(outcome.bytes_transferred);
                    if(request->m_BytesRead >= request->m_BytesRequested)
                    {
                        SDL_CloseAsyncIO(request->m_AsyncIO, false, FFGlobals::AsyncIOQueue, request);
                        request->m_AsyncIO = nullptr;
                        request->SetComplete(RequestStatus::Success);
                    }
                    else if(!IssueRead(*request))
                    {
                        SDL_CloseAsyncIO(request->m_AsyncIO, false, FFGlobals::AsyncIOQueue, request);
                        request->m_AsyncIO = nullptr;
                        request->SetComplete(RequestStatus::Failure);
                    }
                    break;
                case SDL_ASYNCIO_FAILURE:
                    MLG_ERROR("Async IO read failed for file: {}, error: {}",
                        request->m_FilePath,
                        SDL_GetError());
                    SDL_CloseAsyncIO(request->m_AsyncIO, false, FFGlobals::AsyncIOQueue, request);
                    break;
                case SDL_ASYNCIO_CANCELED:
                    MLG_ERROR("Async IO read failed for file: {}, error: {}",
                        request->m_FilePath,
                        SDL_GetError());
                    if(FFGlobals::AsyncIOQueue)
                    {
                        SDL_CloseAsyncIO(request->m_AsyncIO, false, FFGlobals::AsyncIOQueue, request);
                    }
                    break;
            }
        }
        else
        {
            MLG_ERROR("Received unexpected SDL Async IO completion of type: {}",
                static_cast<int>(outcome.type));
            continue;
        }
    }

    return Result<>::Ok;
}

Result<size_t>
FileFetcher::GetFileSize(const FileFetcher::Request& request)
{
    const Sint64 fileSize = SDL_GetAsyncIOSize(request.m_AsyncIO);
    MLG_CHECK(fileSize >= 0,
        "Failed to get file size for file: {}, error: {}",
        request.m_FilePath,
        SDL_GetError());

    return static_cast<size_t>(fileSize);
}

Result<>
FileFetcher::IssueRead(FileFetcher::Request& request)
{
    MLG_CHECK(SDL_ReadAsyncIO(request.m_AsyncIO,
                  request.m_Data.data(),
                  0,
                  request.m_BytesRequested,
                  FFGlobals::AsyncIOQueue,
                  &request),
        "Failed to issue async load for file: {}, error: {}",
        request.m_FilePath,
        SDL_GetError());

    return Result<>::Ok;
}
#endif  // _WIN32