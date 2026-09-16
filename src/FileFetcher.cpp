#include "FileFetcher.h"

#include "Log.h"
#include "scope_exit.h"

#include <cstdint>
#include <memory>
#include <SDL3/SDL_asyncio.h>
#include <SDL3/SDL_error.h>
#include <string>

namespace
{
constexpr uint32_t kShift = 32;

FetchRequestId
MakeFetchRequestId(uint32_t index, uint32_t generation)
{
    return (static_cast<FetchRequestId>(index) << kShift) | generation;
}

uint32_t
GetIndex(FetchRequestId requestId)
{
    return static_cast<uint32_t>(requestId >> kShift);
}

uint32_t
GetGeneration(FetchRequestId requestId)
{
    return static_cast<uint32_t>(requestId & ((static_cast<FetchRequestId>(1) << kShift) - 1));
}
} // namespace

FileFetcher::Request::~Request()
{
    MLG_ASSERT(!IsPending(), "Request destroyed while still pending");
    MLG_ASSERT(!m_AsyncIO, "Request destroyed with active SDL_AsyncIO");
}

FileFetcher::~FileFetcher()
{
    if(!m_IoQueue)
    {
        return;
    }

    ProcessCompletions();

    RequestBuffer* allocatedRequests = nullptr;

    for(auto& bucket : m_RequestBuffers)
    {
        for(auto& requestBuf : bucket)
        {
            if(requestBuf.m_Request)
            {
                if(requestBuf.m_Request->IsPending())
                {
                    SetFailed(requestBuf.m_Request->m_RequestId);
                    Close(requestBuf.m_Request->m_RequestId);
                }
                requestBuf.m_Next = allocatedRequests;
                allocatedRequests = &requestBuf;
            }
        }
    }

    SDL_SignalAsyncIOQueue(m_IoQueue);
    SDL_DestroyAsyncIOQueue(m_IoQueue);

    while(allocatedRequests)
    {
        RequestBuffer* requestBuf = allocatedRequests;
        allocatedRequests = allocatedRequests->m_Next;
        requestBuf->m_Next = nullptr;
        FreeRequest(requestBuf);
    }

    m_IoQueue = nullptr;

    MLG_ASSERT(m_AllocCount == 0, "FileFetcher destroyed with outstanding allocations");
}

Result<std::unique_ptr<FileFetcher>>
FileFetcher::Create()
{
    SDL_AsyncIOQueue* asyncIOQueue = SDL_CreateAsyncIOQueue();
    MLG_CHECKV(asyncIOQueue, "Failed to create SDL Async IO Queue: {}", SDL_GetError());

    std::unique_ptr<FileFetcher> fileFetcher(new FileFetcher(asyncIOQueue));

    return fileFetcher;
}

Result<FetchRequestId>
FileFetcher::Fetch(std::string filePath)
{
    MLG_CHECKV(m_IoQueue, "FileFetcher::Fetch called on invalid FileFetcher instance");

    RequestBuffer* requestBuf = AllocateRequest();
    MLG_CHECKV(requestBuf, "Failed to allocate request buffer for file: {}", filePath);

    Request& request = *requestBuf->m_Request;

    MLG_ASSERT(Request::Status::None == request.m_Status);
    MLG_ASSERT(!request.m_AsyncIO);

    request.m_FilePath = std::move(filePath);
    request.m_Status = Request::Status::Pending;

    // Free resources if we early exit due to an error.
    MLG_DEFER_AS(freeRequest)
    {
        SetFailed(requestBuf->m_Request->m_RequestId);

        Close(requestBuf->m_Request->m_RequestId);

        FreeRequest(requestBuf);
    };

    request.m_AsyncIO = SDL_AsyncIOFromFile(request.m_FilePath.c_str(), "r");
    MLG_CHECK(request.m_AsyncIO,
        "Failed to create SDL Async IO for file: {}, error: {}",
        request.m_FilePath,
        SDL_GetError());

    const Sint64 fileSize = SDL_GetAsyncIOSize(request.m_AsyncIO);
    MLG_CHECK(fileSize >= 0,
        "Failed to get file size for file: {}, error: {}",
        request.m_FilePath,
        SDL_GetError());

    MLG_CHECK(fileSize > 0, "File size is zero for file: {}", request.m_FilePath);

    const size_t fileSizeBytes = static_cast<size_t>(fileSize);

    if(request.m_BytesRequested == 0)
    {
        request.m_BytesRequested = fileSizeBytes;
    }
    else
    {
        MLG_CHECK(request.m_BytesRequested <= fileSizeBytes,
            "Requested byte count {} exceeds file size {} for file: {}",
            request.m_BytesRequested,
            fileSizeBytes,
            request.m_FilePath);
    }

    if(request.m_Data.size() < request.m_BytesRequested)
    {
        request.m_Data.resize(request.m_BytesRequested);
    }

    MLG_CHECK(IssueRead(request));

    // We're returning successfully - cancel the deferrals.
    freeRequest.release();

    return requestBuf->m_Request->m_RequestId;
}

bool
FileFetcher::IsPending(const FetchRequestId requestId) const
{
    const RequestBuffer* requestBuf = GetRequestBuffer(requestId);
    return MLG_VERIFY(requestBuf) && requestBuf->m_Request->IsPending();
}

Result<>
FileFetcher::Take(const FetchRequestId requestId, std::vector<uint8_t>& outBuffer)
{
    RequestBuffer* requestBuf = GetRequestBuffer(requestId);
    MLG_CHECKV(requestBuf, "Invalid request ID: {}", requestId);
    MLG_CHECKV(!requestBuf->m_Request->IsPending(),
        "Request is still pending for file: {}",
        requestBuf->m_Request->m_FilePath);

    MLG_DEFER
    {
        FreeRequest(requestBuf);
    };

    MLG_CHECKV(requestBuf->m_Request->Succeeded(),
        "Request did not succeed for file: {}",
        requestBuf->m_Request->m_FilePath);

    outBuffer = std::move(requestBuf->m_Request->m_Data);

    return Result<>::Ok;
}

void
FileFetcher::ProcessCompletions()
{
    MLG_ABORTIF(!m_IoQueue,
        "FileFetcher::ProcessCompletions called on invalid FileFetcher instance");

    SDL_AsyncIOOutcome outcome;
    while(SDL_GetAsyncIOResult(m_IoQueue, &outcome))
    {
        if(outcome.type != SDL_ASYNCIO_TASK_READ && outcome.type != SDL_ASYNCIO_TASK_CLOSE)
        {
            MLG_ERROR("Received unexpected SDL Async IO completion of type: {}",
                static_cast<int>(outcome.type));
            continue;
        }

        if(outcome.type == SDL_ASYNCIO_TASK_CLOSE)
        {
            continue;
        }

        Request* request = static_cast<Request*>(outcome.userdata);
        MLG_ABORTIF(request == nullptr, "Received SDL Async IO completion with null userdata");

        switch(outcome.result)
        {
            case SDL_ASYNCIO_COMPLETE:
                request->m_BytesRead += static_cast<size_t>(outcome.bytes_transferred);
                if(request->m_BytesRead >= request->m_BytesRequested)
                {
                    SetSucceeded(request->m_RequestId);
                    Close(request->m_RequestId);
                }
                else if(request->m_ReadAttempts >= kMaxReadAttempts || !IssueRead(*request))
                {
                    SetFailed(request->m_RequestId);
                    Close(request->m_RequestId);
                }
                break;
            case SDL_ASYNCIO_FAILURE:
                MLG_ERROR("Async IO {} failed for file: {}, error: {}",
                    (outcome.type == SDL_ASYNCIO_TASK_READ ? "read" : "close"),
                    request->m_FilePath,
                    SDL_GetError());

                SetFailed(request->m_RequestId);
                Close(request->m_RequestId);
                break;
            case SDL_ASYNCIO_CANCELED:
                MLG_ERROR("Async IO {} canceled for file: {}",
                    (outcome.type == SDL_ASYNCIO_TASK_READ ? "read" : "close"),
                    request->m_FilePath);
                if(m_IoQueue)
                {
                    SetFailed(request->m_RequestId);
                    Close(request->m_RequestId);
                }
                break;
        }
    }
}

Result<>
FileFetcher::IssueRead(Request& request)
{
    MLG_CHECKV(m_IoQueue, "FileFetcher::IssueRead called on invalid FileFetcher instance");

    MLG_ASSERT(request.m_AsyncIO);

    const size_t bytesToRead = request.m_BytesRequested - request.m_BytesRead;

    MLG_CHECK(SDL_ReadAsyncIO(request.m_AsyncIO,
                  &request.m_Data[request.m_BytesRead],
                  request.m_BytesRead,
                  bytesToRead,
                  m_IoQueue,
                  &request),
        "Failed to issue async load for file: {}, error: {}",
        request.m_FilePath,
        SDL_GetError());

    ++request.m_ReadAttempts;

    return Result<>::Ok;
}

FileFetcher::RequestBuffer*
FileFetcher::AllocateRequest()
{
    if(!m_FreeList)
    {
        m_RequestBuffers.emplace_back(kBucketSize);
        m_AllocCount += kBucketSize;
        for(RequestBuffer& buffer : m_RequestBuffers.back())
        {
            buffer.m_Index = m_HeapSize++;
            FreeRequest(&buffer);
        }
    }

    RequestBuffer* requestBuf = m_FreeList;
    m_FreeList = m_FreeList->m_Next;
    requestBuf->m_Next = nullptr;
    void* p = static_cast<void*>(requestBuf->m_Storage);
    requestBuf->m_Request = std::construct_at(static_cast<Request*>(p));

    const FetchRequestId requestId =
        MakeFetchRequestId(requestBuf->m_Index, requestBuf->m_Generation);

    requestBuf->m_Request->m_RequestId = requestId;
    ++m_AllocCount;
    return requestBuf;
}

void
FileFetcher::FreeRequest(RequestBuffer* requestBuf)
{
    if(requestBuf)
    {
        MLG_ASSERT(requestBuf->m_Next == nullptr, "Request was already freed");

        if(requestBuf->m_Request)
        {
            MLG_ASSERT(!requestBuf->m_Request->IsPending(),
                "Cannot free a request that is still pending");

            requestBuf->m_Request->~Request();
            requestBuf->m_Request = nullptr;
        }

        ++requestBuf->m_Generation;
        if(kInvalidGeneration == requestBuf->m_Generation)
        {
            ++requestBuf->m_Generation;
        }

        requestBuf->m_Next = m_FreeList;
        m_FreeList = requestBuf;

        MLG_ASSERT(m_AllocCount > 0, "Allocation count underflow");
        --m_AllocCount;
    }
}

void
FileFetcher::SetSucceeded(const FetchRequestId requestId)
{
    const RequestBuffer* requestBuf = GetRequestBuffer(requestId);
    if(MLG_VERIFY(requestBuf) && MLG_VERIFY(requestBuf->m_Request->IsPending()))
    {
        requestBuf->m_Request->m_Status = FileFetcher::Request::Status::Success;
    }
}

void
FileFetcher::SetFailed(const FetchRequestId requestId)
{
    const RequestBuffer* requestBuf = GetRequestBuffer(requestId);
    if(MLG_VERIFY(requestBuf) && MLG_VERIFY(requestBuf->m_Request->IsPending()))
    {
        requestBuf->m_Request->m_Status = FileFetcher::Request::Status::Failure;
    }
}

void
FileFetcher::Close(const FetchRequestId requestId)
{
    const RequestBuffer* requestBuf = GetRequestBuffer(requestId);
    if(MLG_VERIFY(requestBuf) && MLG_VERIFY(!requestBuf->m_Request->IsPending()))
    {
        if(requestBuf->m_Request->m_AsyncIO)
        {
            const bool closed =
                SDL_CloseAsyncIO(requestBuf->m_Request->m_AsyncIO, false, m_IoQueue, nullptr);
            requestBuf->m_Request->m_AsyncIO = nullptr;
            MLG_ABORTIF(!closed, "Failed to close SDL_AsyncIO");
        }
    }
}

FileFetcher::RequestBuffer*
FileFetcher::GetRequestBuffer(const FetchRequestId requestId)
{
    const uint32_t index = GetIndex(requestId);
    const uint32_t generation = GetGeneration(requestId);

    if(!MLG_VERIFY(index < m_HeapSize))
    {
        return nullptr;
    }

    const uint32_t bucket = index / kBucketSize;
    const uint32_t indexInBucket = index % kBucketSize;

    RequestBuffer* requestBuf = &m_RequestBuffers[bucket][indexInBucket];
    if(!MLG_VERIFY(requestBuf->m_Generation == generation))
    {
        return nullptr;
    }

    return requestBuf;
}

const FileFetcher::RequestBuffer*
FileFetcher::GetRequestBuffer(const FetchRequestId requestId) const
{
    return const_cast<FileFetcher*>(this)->GetRequestBuffer(requestId); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}