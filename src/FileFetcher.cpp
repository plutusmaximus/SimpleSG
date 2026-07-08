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

FileFetcher::Request::Request(FileFetcher::Request&& other) noexcept
    : m_AsyncIO(std::exchange(other.m_AsyncIO, nullptr)),
      m_FilePath(std::move(other.m_FilePath)),
      m_BytesRequested(std::exchange(other.m_BytesRequested, 0)),
      m_BytesRead(std::exchange(other.m_BytesRead, 0)),
      m_Data(std::move(other.m_Data)),
      m_Status(std::exchange(other.m_Status, RequestStatus::None))
{
}

FileFetcher::Request&
FileFetcher::Request::operator=(Request&& other) noexcept
{
    if(this == &other)
    {
        return *this;
    }

    m_AsyncIO = std::exchange(other.m_AsyncIO, nullptr);
    m_FilePath = std::move(other.m_FilePath);
    m_BytesRequested = std::exchange(other.m_BytesRequested, 0);
    m_BytesRead = std::exchange(other.m_BytesRead, 0);
    m_Data = std::move(other.m_Data);
    m_Status = std::exchange(other.m_Status, RequestStatus::None);

    return *this;
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

FileFetcher::FileFetcher(mlg::detail::FileFetcherImpl* impl)
: m_Impl(impl)
{
    MLG_ASSERT(m_Impl, "FileFetcherImpl pointer cannot be null");
    MLG_ASSERT(m_Impl->AsyncIOQueue, "AsyncIOQueue pointer cannot be null");
}

FileFetcher::~FileFetcher()
{
    if(!m_Impl)
    {
        return;
    }

    const std::lock_guard lock(m_Impl->Mutex);

    ProcessCompletions();
    SDL_SignalAsyncIOQueue(m_Impl->AsyncIOQueue);
    SDL_DestroyAsyncIOQueue(m_Impl->AsyncIOQueue);
    m_Impl->AsyncIOQueue = nullptr;

    delete m_Impl;
    m_Impl = nullptr;
}

Result<FileFetcher>
FileFetcher::Create()
{
    auto impl = std::make_unique<mlg::detail::FileFetcherImpl>();
    impl->AsyncIOQueue = SDL_CreateAsyncIOQueue();

    if(!MLG_VERIFY(impl->AsyncIOQueue,
        "Failed to create SDL Async IO Queue: {}",
        SDL_GetError()))
    {
        return Result<FileFetcher>::Fail;
    }

    return FileFetcher(impl.release());
}

FileFetcher::FileFetcher(FileFetcher&& other) noexcept
: m_Impl(std::exchange(other.m_Impl, nullptr))
{
}

FileFetcher& FileFetcher::operator=(FileFetcher&& other) noexcept
{
    if(this == &other)
    {
        return *this;
    }

    delete m_Impl;
    m_Impl = std::exchange(other.m_Impl, nullptr);
    return *this;
}

Result<>
FileFetcher::Fetch(FileFetcher::Request& request)
{
    MLG_CHECKV(m_Impl, "FileFetcher::Fetch called on invalid FileFetcher instance");

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
    MLG_CHECKV(m_Impl, "FileFetcher::ProcessCompletions called on invalid FileFetcher instance");

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
    MLG_CHECKV(m_Impl, "FileFetcher::IssueRead called on invalid FileFetcher instance");

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