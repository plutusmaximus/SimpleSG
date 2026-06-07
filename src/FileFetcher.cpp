#include "FileFetcher.h"

#include "Log.h"

#include "SanitizerHelpers.h"
#include  "scope_exit.h"

#include <mutex>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_asyncio.h>

namespace
{
std::mutex* MakeMutex()
{
    std::mutex* p = new std::mutex; // NOLINT(cppcoreguidelines-owning-memory)

    // We intentionally leak this, so hide it from leak sanitizers
    __lsan_ignore_object(p);

    return p;
}

std::mutex& GetMutex()
{
    static std::mutex* mutex = MakeMutex();
    return *mutex;
}

struct FFGlobals
{
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
        const std::lock_guard lock(GetMutex());
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
    const std::lock_guard lock(GetMutex());
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

    MLG_DEFER_AS(setFailedOnExit)
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

    MLG_DEFER_AS(closeOnFailure)
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