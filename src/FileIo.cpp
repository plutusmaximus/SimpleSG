#include "FileIo.h"
#include "PoolAllocator.h"

#include <array>
#include <atomic>
#include <cstring>
#include <format>
#include <limits>
#include <mutex>

class ReadRequest
{
public:
    explicit ReadRequest(const imstring& path)
        : Path(path)
    {
    }

    ReadRequest(const ReadRequest&) = delete;
    ReadRequest& operator=(const ReadRequest&) = delete;
    ReadRequest(ReadRequest&&) = delete;
    ReadRequest& operator=(ReadRequest&&) = delete;

    virtual ~ReadRequest() = 0;

    void Link(ReadRequest* next)
    {
        eassert(!m_Next);
        eassert(!m_Prev);

        m_Next = next;
        if(next)
        {
            next->m_Prev = this;
        }
    }

    void Unlink()
    {
        if(m_Prev)
        {
            m_Prev->m_Next = m_Next;
        }
        if(m_Next)
        {
            m_Next->m_Prev = m_Prev;
        }
        m_Next = nullptr;
        m_Prev = nullptr;
    }

    imstring Path;

    FileIo::AsyncToken Token = FileIo::AsyncToken::NewToken();

    std::optional<Error> Error;

    ReadRequest* m_Next{ nullptr };
    ReadRequest* m_Prev{ nullptr };
};

ReadRequest::~ReadRequest() = default;

FileIo::FetchData::~FetchData() = default;

enum State
{
    NotStarted,
    Running,
    FatalError,
};

static std::atomic<State> s_State{ NotStarted };

static std::mutex s_Mutex;
static std::mutex s_IOCPMutex;
static ReadRequest* s_Pending{ nullptr };
static ReadRequest* s_Complete{ nullptr };

static bool
IsRunning()
{
    return s_State.load(std::memory_order_acquire) == State::Running;
}

static bool
HaveFatalError()
{
    return s_State.load(std::memory_order_acquire) == State::FatalError;
}

static bool
IsShutdown()
{
    return s_State.load(std::memory_order_acquire) == State::NotStarted;
}

static void
AddPendingRequest(ReadRequest* req)
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    req->Link(s_Pending);
    s_Pending = req;
}

static void
MoveFromPendingToComplete(ReadRequest* req)
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    if(s_Pending == req)
    {
        s_Pending = req->m_Next;
    }

    req->Unlink();

    req->Link(s_Complete);
    s_Complete = req;
}

static void
RemoveCompleteRequest(ReadRequest* req)
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    if(s_Complete == req)
    {
        s_Complete = req->m_Next;
    }

    req->Unlink();
}

template<typename T, typename... Args>
static T* NewReadRequest(Args&&... args);

static void DeleteReadRequest(ReadRequest* req);

// Platform-specific implementation used by GetResult.
static Result<FileIo::FetchDataPtr> GetResultImpl(ReadRequest* req);

bool
FileIo::Startup()
{
    if(!IsShutdown())
    {
        return true; // Already started or starting
    }

    if(!PlatformStartup())
    {
        return false;
    }

    s_State.store(State::Running, std::memory_order_release);

    return true;
}

void
FileIo::Shutdown()
{
    if(IsShutdown())
    {
        return; // Already shutdown
    }

    PlatformShutdown();

    // Drain any remaining completed requests.
    ReadRequest* complete = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        std::swap(complete, s_Complete);
    }

    for(ReadRequest* req = complete; req != nullptr;)
    {
        ReadRequest* next = req->m_Next;
        DeleteReadRequest(req);
        req = next;
    }

    s_State.store(State::NotStarted, std::memory_order_release);
}

FileIo::FetchStatus
FileIo::GetStatus(const AsyncToken token)
{
    if(HaveFatalError())
    {
        Shutdown();
    }

    if(!IsRunning())
    {
        return FetchStatus::None;
    }

    ProcessCompletions();

    std::lock_guard<std::mutex> lock(s_Mutex);

    for(ReadRequest* req = s_Pending; req != nullptr; req = req->m_Next)
    {
        if(req->Token == token)
        {
            return FetchStatus::Pending;
        }
    }

    for(ReadRequest* req = s_Complete; req != nullptr; req = req->m_Next)
    {
        if(req->Token == token)
        {
            return FetchStatus::Completed;
        }
    }

    return FetchStatus::None;
}

Result<FileIo::FetchDataPtr>
FileIo::GetResult(const AsyncToken token)
{
    if(HaveFatalError())
    {
        Shutdown();
    }

    if(!IsRunning())
    {
        return Error("FileIo is not running or is shutting down.");
    }

    ProcessCompletions();

    ReadRequest* req;

    {
        std::lock_guard<std::mutex> lock(s_Mutex);

        for(req = s_Complete; req != nullptr; req = req->m_Next)
        {
            if(req->Token == token)
            {
                break;
            }
        }
    }

    if(!req)
    {
        return Error("No completed request found for given token.");
    }

    RemoveCompleteRequest(req);

    Result<FileIo::FetchDataPtr> result;

    if(req->Error)
    {
        result = req->Error.value();
    }
    else
    {
        result = GetResultImpl(req);
    }

    DeleteReadRequest(req);

    return result;
}

FileIo::AsyncToken
FileIo::AsyncToken::NewToken()
{
    static std::atomic<ValueType> s_NextToken{ 1 };
    ValueType tokenValue;
    do
    {
        tokenValue = s_NextToken.fetch_add(1, std::memory_order_acquire);
    } while(InvalidValue == tokenValue);

    return AsyncToken(tokenValue);
}

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

static HANDLE s_IOCP = nullptr;

static Result<void> IssueReadRequest(ReadRequest* req);
static std::string GetWindowsErrorString(DWORD errorCode);

struct Win32ReadRequest : ReadRequest
{
    Win32ReadRequest(const imstring& path,
        HANDLE hFile,
        std::unique_ptr<uint8_t[]> bytes,
        const size_t bytesRequested)
        : ReadRequest(path),
          File(hFile),
          Bytes(std::move(bytes)),
          BytesRequested(bytesRequested)
    {
    }

    Win32ReadRequest() = delete;
    Win32ReadRequest(const Win32ReadRequest&) = delete;
    Win32ReadRequest& operator=(const Win32ReadRequest&) = delete;
    Win32ReadRequest(Win32ReadRequest&&) = delete;
    Win32ReadRequest& operator=(Win32ReadRequest&&) = delete;

    ~Win32ReadRequest() override
    {
        if(File != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(File);
            File = INVALID_HANDLE_VALUE;
        }
    }

    HANDLE File{ INVALID_HANDLE_VALUE };
    OVERLAPPED Ov{ 0 };
    std::unique_ptr<uint8_t[]> Bytes;
    const size_t BytesRequested{ 0 };
    size_t BytesRead{ 0 };
};

static PoolAllocator<Win32ReadRequest, 64> s_ReadRequestPool;

template<typename... Args>
static Win32ReadRequest* AllocReadRequest(Args&&... args)
{
    return s_ReadRequestPool.New(std::forward<Args>(args)...);
}

static void DeleteReadRequest(ReadRequest* req)
{
    auto* win32Req = static_cast<Win32ReadRequest*>(req);
    s_ReadRequestPool.Delete(win32Req);
}

Result<FileIo::AsyncToken>
FileIo::Fetch(const imstring& filePath)
{
    if(!IsRunning())
    {
        return Error("FileIO is not running or is shutting down.");
    }

    // Open file
    HANDLE hFile = ::CreateFileA(filePath.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if(hFile == INVALID_HANDLE_VALUE)
    {
        // Failed to open file; fulfill promise with 0 bytes read.
        return Error("Failed to open file: {}, error: {}",
            filePath,
            GetWindowsErrorString(::GetLastError()));
    }

    LARGE_INTEGER fsize;
    if(!::GetFileSizeEx(hFile, &fsize))
    {
        ::CloseHandle(hFile);

        return Error("Failed to get file size: {}, error: {}",
            filePath,
            GetWindowsErrorString(::GetLastError()));
    }

    const int64_t fileSize = fsize.QuadPart;

    if(fileSize < 0)
    {
        ::CloseHandle(hFile);

        return Error("Failed to get file size for {}.", filePath);
    }

    if(fileSize > std::numeric_limits<DWORD>::max())
    {
        ::CloseHandle(hFile);

        return Error("File is too large to read: {}", filePath);
    }

    if(!fileSize)
    {
        ::CloseHandle(hFile);

        return Error("File is empty: {}", filePath);
    }

    std::unique_ptr<uint8_t[]> bytes = std::make_unique<uint8_t[]>(fileSize);
    if(!bytes)
    {
        ::CloseHandle(hFile);

        return Error("Failed to allocate read buffer for file: {}", filePath);
    }

    auto req = AllocReadRequest(filePath, hFile, std::move(bytes), static_cast<size_t>(fileSize));

    if(!req)
    {
        return Error("Failed to allocate read request.");
    }

    // Used as key to identify the request on completion.
    ULONG_PTR key = reinterpret_cast<ULONG_PTR>(req);

    // Bind file to IOCP.
    if(::CreateIoCompletionPort(req->File, s_IOCP, key, 0) == nullptr)
    {
        DeleteReadRequest(req);
        return Error("Failed to bind file to IOCP: {}, error: {}",
            filePath,
            GetWindowsErrorString(::GetLastError()));
    }

    AddPendingRequest(req);

    auto result = IssueReadRequest(req);

    if(!result)
    {
        CompleteRequestFailure(req, result.error());
    }
    else if(req->BytesRead >= req->BytesRequested)
    {
        CompleteRequestSuccess(req, static_cast<size_t>(req->BytesRead));
    }

    return req->Token;
}

void
FileIo::ProcessCompletions()
{
    if(!IsRunning())
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        if(!s_Pending)
        {
            // Nothing pending
            return;
        }
    }

    BOOL ok;
    std::array<OVERLAPPED_ENTRY, 8> entries = {};
    ULONG numEntriesRemoved = 0;
    {
        std::lock_guard<std::mutex> lock(s_IOCPMutex);

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
            return;
        }

        if(ERROR_ABANDONED_WAIT_0 == err)
        {
            // IOCP was closed during shutdown.
            return;
        }

        // Some other error occurred - assume it's fatal.
        s_State.store(State::FatalError, std::memory_order_release);

        return;
    }

    // If we get here, at least one read completed successfully.

    for(ULONG i = 0; i < numEntriesRemoved; ++i)
    {
        OVERLAPPED_ENTRY& entry = entries[i];
        Win32ReadRequest* req = reinterpret_cast<Win32ReadRequest*>(entry.lpCompletionKey);

        if(!req)
        {
            continue;
        }

        const DWORD bytesRead = entry.dwNumberOfBytesTransferred;

        req->BytesRead += bytesRead;

        if(req->BytesRead < req->BytesRequested)
        {
            // Read more bytes
            auto result = IssueReadRequest(req);

            if(!result)
            {
                CompleteRequestFailure(req, result.error());
                continue;
            }
        }

        if(req->BytesRead >= req->BytesRequested)
        {
            CompleteRequestSuccess(req, static_cast<size_t>(req->BytesRead));
        }
    }

    return;
}

static Result<void>
IssueReadRequest(ReadRequest* req)
{
    Win32ReadRequest* win32Req = static_cast<Win32ReadRequest*>(req);

    bool done = false;

    while(win32Req->BytesRead < win32Req->BytesRequested && !done)
    {
        LARGE_INTEGER li;
        li.QuadPart = win32Req->BytesRead;

        // Set up the offset into the file from which to read.
        win32Req->Ov.Offset = li.LowPart;
        win32Req->Ov.OffsetHigh = li.HighPart;

        const size_t bytesRemaining = win32Req->BytesRequested - win32Req->BytesRead;

        DWORD bytesRead = 0;

        // Re-issue read for remaining bytes
        const BOOL ok = ::ReadFile(win32Req->File,
            win32Req->Bytes.get() + win32Req->BytesRead,
            static_cast<DWORD>(bytesRemaining),
            &bytesRead,
            &win32Req->Ov);

        if(ok)
        {
            // Request completed synchronously - loop again if necessary
            win32Req->BytesRead += bytesRead;
            continue;
        }

        const DWORD err = ::GetLastError();

        if(err == ERROR_IO_PENDING)
        {
            // Still pending, break out of the loop.
            done = true;
            continue;
        }

        return Error("Failed to issue read for file: {}, error: {}",
            win32Req->Path,
            GetWindowsErrorString(err));
    }

    return Result<void>::Success;
}

// private:

bool
FileIo::PlatformStartup()
{
    s_IOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if(s_IOCP == nullptr)
    {
        return false;
    }

    return true;
}

bool
FileIo::PlatformShutdown()
{
    ReadRequest* pending = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        std::swap(pending, s_Pending);
    }

    // Cancel any pending IO
    for(ReadRequest *req = pending, *next = nullptr; req != nullptr; req = next)
    {
        next = req->m_Next;

        req->Unlink();

        auto win32Req = static_cast<Win32ReadRequest*>(req);

        if(win32Req->File != INVALID_HANDLE_VALUE)
        {
            ::CancelIoEx(win32Req->File, &win32Req->Ov);
        }

        CompleteRequestFailure(req, "Async read cancelled due to shutdown");
    }

    if(s_IOCP != nullptr)
    {
        ::CloseHandle(s_IOCP);
        s_IOCP = nullptr;
    }

    return true;
}

void
FileIo::CompleteRequestSuccess(ReadRequest* request, const size_t bytesRead)
{
    Win32ReadRequest* req = static_cast<Win32ReadRequest*>(request);

    req->BytesRead = bytesRead;

    if(req->File != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(req->File);
        req->File = INVALID_HANDLE_VALUE;
    }

    MoveFromPendingToComplete(req);
}

void
FileIo::CompleteRequestFailure(ReadRequest* request, const Error& error)
{
    Win32ReadRequest* req = static_cast<Win32ReadRequest*>(request);

    req->Error = error;

    if(req->File != INVALID_HANDLE_VALUE)
    {
        ::CloseHandle(req->File);
        req->File = INVALID_HANDLE_VALUE;
    }

    MoveFromPendingToComplete(req);
}

static std::string
GetWindowsErrorString(DWORD errorCode)
{
    LPSTR messageBuffer = nullptr;
    size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                     FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        errorCode,
        MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
        (LPSTR)&messageBuffer,
        0,
        nullptr);

    if(size == 0)
    {
        return std::format("Unknown error code: {}", errorCode);
    }

    std::string message(messageBuffer, size);

    LocalFree(messageBuffer);

    return message;
}

struct Win32FetchData : FileIo::FetchData
{
    Win32FetchData(std::unique_ptr<uint8_t[]>&& bytes, size_t bytesRead)
        : FetchData(bytes.get(), bytesRead),
          m_Bytes(std::move(bytes))
    {
    }

private:
    std::unique_ptr<uint8_t[]> m_Bytes{ nullptr };
};

static Result<FileIo::FetchDataPtr>
GetResultImpl(ReadRequest* req)
{
    auto win32Req = static_cast<Win32ReadRequest*>(req);

    auto fetchData =
        std::make_unique<Win32FetchData>(std::move(win32Req->Bytes), win32Req->BytesRead);

    auto result = FileIo::FetchDataPtr(std::move(fetchData));

    return result;
}

#elif defined(__EMSCRIPTEN__)

#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>

struct EmscriptenReadRequest : ReadRequest
{
    EmscriptenReadRequest(const imstring& path)
        : ReadRequest(path)
    {
    }

    EmscriptenReadRequest() = delete;
    EmscriptenReadRequest(const EmscriptenReadRequest&) = delete;
    EmscriptenReadRequest& operator=(const EmscriptenReadRequest&) = delete;
    EmscriptenReadRequest(EmscriptenReadRequest&&) = delete;
    EmscriptenReadRequest& operator=(EmscriptenReadRequest&&) = delete;

    ~EmscriptenReadRequest() override
    {
        if(FetchData)
        {
            emscripten_fetch_close(FetchData);
        }
    }

    emscripten_fetch_t* FetchData{ nullptr };
};


static PoolAllocator<EmscriptenReadRequest, 64> s_ReadRequestPool;

template<typename... Args>
static EmscriptenReadRequest* AllocReadRequest(Args&&... args)
{
    return s_ReadRequestPool.Alloc(std::forward<Args>(args)...);
}

static void FreeReadRequest(ReadRequest* req)
{
    auto* emReq = static_cast<EmscriptenReadRequest*>(req);
    s_ReadRequestPool.Free(emReq);
}

Result<FileIo::StatusToken>
FileIo::Fetch(const imstring& filePath)
{
    if(!IsRunning())
    {
        return std::unexpected("FileIO is not running or is shutting down.");
    }

    emscripten_fetch_attr_t attr{};
    emscripten_fetch_attr_init(&attr);

    auto* reqPtr = AllocReadRequest(filePath);

    if(!reqPtr)
    {
        return std::unexpected("Failed to allocate read request.");
    }

    std::strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.userData = reqPtr;

    attr.onsuccess = [](emscripten_fetch_t* fetch)
    {
        auto* req = static_cast<EmscriptenReadRequest*>(fetch->userData);

        CompleteRequestSuccess(req, static_cast<size_t>(fetch->numBytes));
    };
    attr.onerror = [](emscripten_fetch_t* fetch)
    {
        auto* req = static_cast<EmscriptenReadRequest*>(fetch->userData);
        Error error("Failed to fetch file: {}, status: {}/{}",
            fetch->url,
            fetch->status,
            fetch->statusText);
        CompleteRequestFailure(req, error);
    };

    reqPtr->FetchData = emscripten_fetch(&attr, reqPtr->Path.c_str());

    // Pending fetch; will be completed by fetch callbacks.
    AddPendingRequest(reqPtr);

    return reqPtr->Token;
}

// private:

bool
FileIo::PlatformStartup()
{
    return true;
}

bool
FileIo::PlatformShutdown()
{
    ReadRequest* pending = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        std::swap(pending, s_Pending);
    }

    for(ReadRequest *req = pending, *next = nullptr; req != nullptr; req = next)
    {
        next = req->m_Next;

        req->Unlink();

        auto* emReq = static_cast<EmscriptenReadRequest*>(req);

        if(emReq->FetchData)
        {
            emscripten_fetch_close(emReq->FetchData);
            emReq->FetchData = nullptr;
        }

        FreeReadRequest(req);
    }

    return true;
}

void
FileIo::ProcessCompletions()
{
    return;
}

void
FileIo::CompleteRequestSuccess(ReadRequest* request, const size_t bytesRead)
{
    MoveFromPendingToComplete(request);
}

void
FileIo::CompleteRequestFailure(ReadRequest* request, const Error& error)
{
    request->Error = error;

    MoveFromPendingToComplete(request);
}

struct EmscriptenFetchData : FileIo::FetchData
{
    explicit EmscriptenFetchData(emscripten_fetch_t* fetch)
        : FetchData(reinterpret_cast<const uint8_t*>(fetch->data), fetch->numBytes),
          m_Fetch(fetch)
    {
    }

    ~EmscriptenFetchData() override
    {
        if(m_Fetch)
        {
            emscripten_fetch_close(m_Fetch);
            m_Fetch = nullptr;
        }
    }

private:
    emscripten_fetch_t* m_Fetch{ nullptr };
};

static Result<FileIo::FetchDataPtr>
GetResultImpl(ReadRequest* req)
{
    auto emReq = static_cast<EmscriptenReadRequest*>(req);

    auto fetchData = std::make_unique<EmscriptenFetchData>(emReq->FetchData);

    emReq->FetchData = nullptr;

    auto result = FileIo::FetchDataPtr(std::move(fetchData));

    return result;
}

#endif // _WIN32 && !__EMSCRIPTEN__