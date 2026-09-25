#include "FileFetcher.h"

#include "Log.h"
#include "scope_exit.h"

#include <cstdint>
#include <memory>
#include <SDL3/SDL_asyncio.h>
#include <SDL3/SDL_error.h>
#include <string>
#include <utility>

FileFetcher::Request::~Request()
{
    MLG_ASSERT(Stage::None == m_Stage || !IsPending(), "Request destroyed while still pending");
    MLG_ASSERT(!m_AsyncIO, "Request destroyed with active SDL_AsyncIO");
}

FileFetcher::~FileFetcher()
{
    if(!m_IoQueue)
    {
        return;
    }

    ProcessCompletions();

    RequestWrapper* allocatedRequests = nullptr;

    for(auto& bucket : m_RequestPool)
    {
        for(auto& wrapper : bucket)
        {
            if(wrapper.m_Request)
            {
                if(wrapper.m_Request->IsPending())
                {
                    SetFailed(wrapper.m_RequestId);
                    Close(wrapper.m_RequestId);
                }
                wrapper.m_Next = allocatedRequests;
                allocatedRequests = &wrapper;
            }
        }
    }

    SDL_SignalAsyncIOQueue(m_IoQueue);
    SDL_DestroyAsyncIOQueue(m_IoQueue);

    while(allocatedRequests)
    {
        RequestWrapper* wrapper = allocatedRequests;
        allocatedRequests = allocatedRequests->m_Next;
        wrapper->m_Next = nullptr;
        FreeRequest(wrapper);
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

    RequestWrapper* wrapper = AllocateRequest();
    MLG_CHECKV(wrapper, "Failed to allocate request buffer for file: {}", filePath);

    Request& request = *wrapper->m_Request;

    MLG_ASSERT(Request::Stage::None == request.m_Stage);
    MLG_ASSERT(!request.m_AsyncIO);

    request.m_FilePath = std::move(filePath);
    request.m_Stage = Request::Stage::Pending;

    // Free resources if we early exit due to an error.
    MLG_DEFER_AS(freeRequest)
    {
        SetFailed(wrapper->m_RequestId);

        Close(wrapper->m_RequestId);

        FreeRequest(wrapper);
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

    MLG_CHECKV(std::in_range<size_t>(fileSize),
        "File size {} exceeds maximum supported size for file: {}",
        fileSize,
        request.m_FilePath);

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

    MLG_CHECK(IssueRead(*wrapper));

    // We're returning successfully - cancel the deferrals.
    freeRequest.release();

    return wrapper->m_RequestId;
}

bool
FileFetcher::IsPending(const FetchRequestId requestId) const
{
    const RequestWrapper* wrapper = GetRequest(requestId);
    return MLG_VERIFY(wrapper) && wrapper->m_Request->IsPending();
}

Result<>
FileFetcher::Take(const FetchRequestId requestId, std::vector<uint8_t>& outBuffer)
{
    RequestWrapper* wrapper = GetRequest(requestId);
    MLG_CHECKV(wrapper, "Invalid request ID");
    MLG_CHECKV(!wrapper->m_Request->IsPending(),
        "Request is still pending for file: {}",
        wrapper->m_Request->m_FilePath);

    MLG_DEFER
    {
        FreeRequest(wrapper);
    };

    MLG_CHECKV(wrapper->m_Request->Succeeded(),
        "Request did not succeed for file: {}",
        wrapper->m_Request->m_FilePath);

    outBuffer = std::move(wrapper->m_Request->m_Data);

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

        RequestWrapper* wrapper = static_cast<RequestWrapper*>(outcome.userdata);
        MLG_ABORTIF(wrapper == nullptr, "Received SDL Async IO completion with null userdata");

        switch(outcome.result)
        {
            case SDL_ASYNCIO_COMPLETE:
                wrapper->m_Request->m_BytesRead +=
                    static_cast<size_t>(outcome.bytes_transferred);
                if(wrapper->m_Request->m_BytesRead >= wrapper->m_Request->m_BytesRequested)
                {
                    SetSucceeded(wrapper->m_RequestId);
                    Close(wrapper->m_RequestId);
                }
                else if(wrapper->m_Request->m_ReadAttempts >= kMaxReadAttempts
                    || !IssueRead(*wrapper))
                {
                    SetFailed(wrapper->m_RequestId);
                    Close(wrapper->m_RequestId);
                }
                break;
            case SDL_ASYNCIO_FAILURE:
                MLG_ERROR("Async IO {} failed for file: {}, error: {}",
                    (outcome.type == SDL_ASYNCIO_TASK_READ ? "read" : "close"),
                    wrapper->m_Request->m_FilePath,
                    SDL_GetError());

                SetFailed(wrapper->m_RequestId);
                Close(wrapper->m_RequestId);
                break;
            case SDL_ASYNCIO_CANCELED:
                MLG_ERROR("Async IO {} canceled for file: {}",
                    (outcome.type == SDL_ASYNCIO_TASK_READ ? "read" : "close"),
                    wrapper->m_Request->m_FilePath);
                if(m_IoQueue)
                {
                    SetFailed(wrapper->m_RequestId);
                    Close(wrapper->m_RequestId);
                }
                break;
        }
    }
}

Result<>
FileFetcher::IssueRead(RequestWrapper& wrapper)
{
    MLG_CHECKV(m_IoQueue, "FileFetcher::IssueRead called on invalid FileFetcher instance");

    MLG_ASSERT(wrapper.m_Request->m_AsyncIO);

    const size_t bytesToRead =
        wrapper.m_Request->m_BytesRequested - wrapper.m_Request->m_BytesRead;

    MLG_CHECK(SDL_ReadAsyncIO(wrapper.m_Request->m_AsyncIO,
                  &wrapper.m_Request->m_Data[wrapper.m_Request->m_BytesRead],
                  wrapper.m_Request->m_BytesRead,
                  bytesToRead,
                  m_IoQueue,
                  &wrapper),
        "Failed to issue async load for file: {}, error: {}",
        wrapper.m_Request->m_FilePath,
        SDL_GetError());

    ++wrapper.m_Request->m_ReadAttempts;

    return Result<>::Ok;
}

FileFetcher::RequestWrapper*
FileFetcher::AllocateRequest()
{
    if(!m_FreeList)
    {
        m_RequestPool.emplace_back(kBucketSize);
        m_AllocCount += kBucketSize;
        for(RequestWrapper& wrapper : m_RequestPool.back())
        {
            wrapper.m_RequestId.m_Index = m_HeapSize++;

            // Initialize the request ID to the next generation
            // to avoid having the default-initialized request ID collide with a valid one.
            wrapper.m_RequestId = wrapper.m_RequestId.GetNextGeneration();

            // Call FreeRequest to add the buffer to the free list.
            FreeRequest(&wrapper);
        }
    }

    RequestWrapper* wrapper = m_FreeList;
    m_FreeList = m_FreeList->m_Next;
    wrapper->m_Next = nullptr;
    void* p = static_cast<void*>(wrapper->m_Storage);
    wrapper->m_Request = std::construct_at(static_cast<Request*>(p));

    ++m_AllocCount;
    return wrapper;
}

void
FileFetcher::FreeRequest(RequestWrapper* wrapper)
{
    if(wrapper)
    {
        MLG_ASSERT(wrapper->m_Next == nullptr, "Request was already freed");

        if(wrapper->m_Request)
        {
            MLG_ASSERT(!wrapper->m_Request->IsPending(),
                "Cannot free a request that is still pending");

            wrapper->m_Request->~Request();
            wrapper->m_Request = nullptr;
        }

        wrapper->m_RequestId = wrapper->m_RequestId.GetNextGeneration();

        wrapper->m_Next = m_FreeList;
        m_FreeList = wrapper;

        MLG_ASSERT(m_AllocCount > 0, "Allocation count underflow");
        --m_AllocCount;
    }
}

void
FileFetcher::SetSucceeded(const FetchRequestId requestId)
{
    const RequestWrapper* wrapper = GetRequest(requestId);
    if(MLG_VERIFY(wrapper) && MLG_VERIFY(wrapper->m_Request->IsPending()))
    {
        wrapper->m_Request->m_Stage = FileFetcher::Request::Stage::Success;
    }
}

void
FileFetcher::SetFailed(const FetchRequestId requestId)
{
    const RequestWrapper* wrapper = GetRequest(requestId);
    if(MLG_VERIFY(wrapper) && MLG_VERIFY(wrapper->m_Request->IsPending()))
    {
        wrapper->m_Request->m_Stage = FileFetcher::Request::Stage::Failure;
    }
}

void
FileFetcher::Close(const FetchRequestId requestId)
{
    const RequestWrapper* wrapper = GetRequest(requestId);
    if(MLG_VERIFY(wrapper) && MLG_VERIFY(!wrapper->m_Request->IsPending()))
    {
        if(wrapper->m_Request->m_AsyncIO)
        {
            const bool closed =
                SDL_CloseAsyncIO(wrapper->m_Request->m_AsyncIO, false, m_IoQueue, nullptr);
            wrapper->m_Request->m_AsyncIO = nullptr;
            MLG_ABORTIF(!closed, "Failed to close SDL_AsyncIO");
        }
    }
}

FileFetcher::RequestWrapper*
FileFetcher::GetRequest(const FetchRequestId requestId)
{
    if(!MLG_VERIFY(requestId.m_Index < m_HeapSize))
    {
        return nullptr;
    }

    const uint32_t bucket = requestId.m_Index / kBucketSize;
    const uint32_t indexInBucket = requestId.m_Index % kBucketSize;

    if(!MLG_VERIFY(bucket < m_RequestPool.size())
        || !MLG_VERIFY(indexInBucket < m_RequestPool[bucket].size()))
    {
        return nullptr;
    }

    RequestWrapper* wrapper = &m_RequestPool[bucket][indexInBucket];
    if(!MLG_VERIFY(wrapper->m_RequestId == requestId))
    {
        return nullptr;
    }

    return wrapper;
}

const FileFetcher::RequestWrapper*
FileFetcher::GetRequest(const FetchRequestId requestId) const
{
    return const_cast<FileFetcher*>(this)->GetRequest(requestId); // NOLINT(cppcoreguidelines-pro-type-const-cast)
}