#include "FileFetcher.h"

#include "Log.h"

#include  "scope_exit.h"

#include <mutex>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_asyncio.h>

namespace mlg::detail
{
struct FileFetcherImpl
{
    SDL_AsyncIOQueue* AsyncIOQueue{nullptr};

    std::mutex Mutex;
};
} // namespace mlg::detail

using namespace mlg::detail;

FileFetcher::Request::Request(std::string filePath)
    : m_FilePath(std::move(filePath))
{
}

FileFetcher::Request::~Request()
{
    if(IsPending())
    {
        SetComplete(RequestStatus::Failure);
    }
}

std::span<const uint8_t>
FileFetcher::Request::GetData() const
{
    MLG_ASSERT(Succeeded(),
        "Attempted to access data of a request that did not succeed or is still pending");
    return m_Data;
}

void
FileFetcher::Request::MoveDataTo(std::vector<uint8_t>& outBuffer)
{
    MLG_ASSERT(Succeeded(),
        "Attempted to move data from a request that did not succeed or is still pending");
    outBuffer = std::move(m_Data);
}

void
FileFetcher::Request::SetComplete(RequestStatus status)
{
    MLG_ASSERT(RequestStatus::Pending == m_Status,
        "Attempted to complete a request that is not pending");
    MLG_ASSERT(status == RequestStatus::Success || status == RequestStatus::Failure,
        "Invalid status for completion");

    m_Status = status;
}

FileFetcher::FileFetcher()
{
    static_assert(sizeof(FileFetcherImpl) <= kSizeofImplStorage,
        "FileFetcherImpl is too large for the storage buffer");

    std::construct_at(m_Impl);

    m_Impl->AsyncIOQueue = SDL_CreateAsyncIOQueue();

    MLG_ABORTIF(!m_Impl->AsyncIOQueue, "Failed to create SDL Async IO Queue: {}", SDL_GetError());
}

FileFetcher::~FileFetcher()
{
    const std::lock_guard lock(m_Impl->Mutex);

    ProcessCompletions();
    SDL_SignalAsyncIOQueue(m_Impl->AsyncIOQueue);
    SDL_DestroyAsyncIOQueue(m_Impl->AsyncIOQueue);
    m_Impl->AsyncIOQueue = nullptr;

    std::destroy_at(m_Impl);
}

Result<>
FileFetcher::Fetch(FileFetcher::Request& request)
{
    MLG_CHECKV(RequestStatus::None == request.m_Status,
        "Request is already in progress or completed");

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
        SDL_CloseAsyncIO(request.m_AsyncIO, false, m_Impl->AsyncIOQueue, &request);
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
    SDL_AsyncIOOutcome outcome;
    while(SDL_GetAsyncIOResult(m_Impl->AsyncIOQueue, &outcome))
    {
        if(outcome.type == SDL_ASYNCIO_TASK_CLOSE)
        {
            continue;
        }

        if(outcome.type != SDL_ASYNCIO_TASK_READ)
        {
            MLG_ERROR("Received unexpected SDL Async IO completion of type: {}",
                static_cast<int>(outcome.type));
            continue;
        }

        Request* request = static_cast<Request*>(outcome.userdata);
        MLG_CHECK(request, "Received SDL Async IO completion with null userdata");

        switch(outcome.result)
        {
            case SDL_ASYNCIO_COMPLETE:
                request->m_BytesRead += static_cast<size_t>(outcome.bytes_transferred);
                if(request->m_BytesRead >= request->m_BytesRequested)
                {
                    SDL_CloseAsyncIO(request->m_AsyncIO, false, m_Impl->AsyncIOQueue, request);
                    request->m_AsyncIO = nullptr;
                    request->SetComplete(RequestStatus::Success);
                }
                else if(!IssueRead(*request))
                {
                    SDL_CloseAsyncIO(request->m_AsyncIO, false, m_Impl->AsyncIOQueue, request);
                    request->m_AsyncIO = nullptr;
                    request->SetComplete(RequestStatus::Failure);
                }
                break;
            case SDL_ASYNCIO_FAILURE:
                MLG_ERROR("Async IO read failed for file: {}, error: {}",
                    request->m_FilePath,
                    SDL_GetError());
                SDL_CloseAsyncIO(request->m_AsyncIO, false, m_Impl->AsyncIOQueue, request);
                // request->SetComplete() will be called when we get the SDL_ASYNCIO_COMPLETE event.
                break;
            case SDL_ASYNCIO_CANCELED:
                MLG_ERROR("Async IO read failed for file: {}, error: {}",
                    request->m_FilePath,
                    SDL_GetError());
                if(m_Impl->AsyncIOQueue)
                {
                    SDL_CloseAsyncIO(request->m_AsyncIO, false, m_Impl->AsyncIOQueue, request);
                }
                // request->SetComplete() will be called when we get the SDL_ASYNCIO_COMPLETE event.
                break;
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
                  m_Impl->AsyncIOQueue,
                  &request),
        "Failed to issue async load for file: {}, error: {}",
        request.m_FilePath,
        SDL_GetError());

    return Result<>::Ok;
}