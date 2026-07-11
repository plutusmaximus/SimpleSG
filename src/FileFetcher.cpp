#include "FileFetcher.h"

#include "Log.h"

#include  "scope_exit.h"

#include <memory>
#include <SDL3/SDL_error.h>
#include <SDL3/SDL_asyncio.h>

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

FileFetcher::~FileFetcher()
{
    if(!m_IoQueue)
    {
        return;
    }

    MLG_VERIFY(ProcessCompletions());
}

Result<FileFetcher>
FileFetcher::Create()
{
    SDL_AsyncIOQueue* asyncIOQueue = SDL_CreateAsyncIOQueue();
    MLG_CHECKV(asyncIOQueue,
        "Failed to create SDL Async IO Queue: {}",
        SDL_GetError());

    return FileFetcher(UniquePtrType(asyncIOQueue, &FileFetcher::Deleter));
}

void
FileFetcher::Deleter(SDL_AsyncIOQueue* asyncIO)
{
    if(!asyncIO)
    {
        return;
    }

    SDL_SignalAsyncIOQueue(asyncIO);
    SDL_DestroyAsyncIOQueue(asyncIO);
}

Result<>
FileFetcher::Fetch(FileFetcher::Request& request)
{
    MLG_CHECKV(m_IoQueue, "FileFetcher::Fetch called on invalid FileFetcher instance");

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

    SDL_AsyncIO* asyncIO = SDL_AsyncIOFromFile(request.m_FilePath.c_str(), "r");
    MLG_CHECK(asyncIO,
        "Failed to create SDL Async IO for file: {}, error: {}",
        request.m_FilePath,
        SDL_GetError());

    MLG_ASSERT(!request.m_AsyncIO, "Request already has an SDL_AsyncIO object");

    request.m_AsyncIO.reset(asyncIO);

    MLG_DEFER_AS(closeOnFailure)
    {
        SDL_CloseAsyncIO(request.m_AsyncIO.get(), false, m_IoQueue.get(), &request);
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
    MLG_CHECKV(m_IoQueue, "FileFetcher::ProcessCompletions called on invalid FileFetcher instance");

    SDL_AsyncIOOutcome outcome;
    while(SDL_GetAsyncIOResult(m_IoQueue.get(), &outcome))
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
        MLG_CHECKV(request, "Received SDL Async IO completion with null userdata");

        switch(outcome.result)
        {
            case SDL_ASYNCIO_COMPLETE:
                request->m_BytesRead += static_cast<size_t>(outcome.bytes_transferred);
                if(request->m_BytesRead >= request->m_BytesRequested)
                {
                    SDL_CloseAsyncIO(request->m_AsyncIO.get(), false, m_IoQueue.get(), request);
                    request->m_AsyncIO = nullptr;
                    request->SetComplete(RequestStatus::Success);
                }
                else if(!IssueRead(*request))
                {
                    SDL_CloseAsyncIO(request->m_AsyncIO.get(), false, m_IoQueue.get(), request);
                    request->m_AsyncIO = nullptr;
                    request->SetComplete(RequestStatus::Failure);
                }
                break;
            case SDL_ASYNCIO_FAILURE:
                MLG_ERROR("Async IO read failed for file: {}, error: {}",
                    request->m_FilePath,
                    SDL_GetError());
                SDL_CloseAsyncIO(request->m_AsyncIO.get(), false, m_IoQueue.get(), request);
                // request->SetComplete() will be called when we get the SDL_ASYNCIO_COMPLETE event.
                break;
            case SDL_ASYNCIO_CANCELED:
                MLG_ERROR("Async IO read failed for file: {}, error: {}",
                    request->m_FilePath,
                    SDL_GetError());
                if(m_IoQueue)
                {
                    SDL_CloseAsyncIO(request->m_AsyncIO.get(), false, m_IoQueue.get(), request);
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
    const Sint64 fileSize = SDL_GetAsyncIOSize(request.m_AsyncIO.get());
    MLG_CHECK(fileSize >= 0,
        "Failed to get file size for file: {}, error: {}",
        request.m_FilePath,
        SDL_GetError());

    return static_cast<size_t>(fileSize);
}

Result<>
FileFetcher::IssueRead(FileFetcher::Request& request)
{
    MLG_CHECKV(m_IoQueue, "FileFetcher::IssueRead called on invalid FileFetcher instance");

    const size_t bytesToRead = request.m_BytesRequested - request.m_BytesRead;

    MLG_CHECK(SDL_ReadAsyncIO(request.m_AsyncIO.get(),
                  request.m_Data.data(),
                  0,
                  bytesToRead,
                  m_IoQueue.get(),
                  &request),
        "Failed to issue async load for file: {}, error: {}",
        request.m_FilePath,
        SDL_GetError());

    return Result<>::Ok;
}