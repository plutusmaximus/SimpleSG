#include "FileIO.h"

#include <atomic>
#include <format>
#include <limits>
#include <memory>
#include <mutex>

struct ReadRequest
{
    ReadRequest(std::string_view path, void* userData, FileIo::ReadCallback callback)
        : Path(path),
          UserData(userData),
          Callback(callback)
    {
    }

    virtual ~ReadRequest() = 0;

    std::string Path;

    const uint8_t* Buffer{ nullptr };
    size_t BytesRead{ 0 };
    std::string Error;

    std::atomic<int> IsComplete{ 0 };

    void* UserData{ nullptr };
    FileIo::ReadCallback Callback{ nullptr };

    template<typename T>
    T* As()
    {
        return static_cast<T*>(this);
    }
    template<typename T>
    const T* As() const
    {
        return static_cast<const T*>(this);
    }

    void Link(ReadRequest* next)
    {
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

    ReadRequest* m_Next{ nullptr };
    ReadRequest* m_Prev{ nullptr };
};

ReadRequest::~ReadRequest() = default;

static std::atomic<int> s_Running{ 0 };

// Set when there's an error in the worker thread that requires shutdown.
static std::atomic<int> s_ShouldShutdown{ 0 };

static std::mutex s_Mutex;
static ReadRequest* s_Pending{ nullptr };
static ReadRequest* s_Complete{ nullptr };

void
AddPendingRequest(ReadRequest* req)
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    req->Link(s_Pending);
    s_Pending = req;
}

void
AddCompletedRequest(ReadRequest* req)
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

bool
FileIo::Startup()
{
    if(s_Running.load(std::memory_order_acquire))
    {
        return true; // Already started
    }

    s_ShouldShutdown.store(0, std::memory_order_release);

    if(!PlatformStartup())
    {
        return false;
    }

    s_Running.store(1, std::memory_order_release);

    return true;
}

void
FileIo::Shutdown()
{
    if(!s_Running.load(std::memory_order_acquire))
    {
        return; // Already shutdown
    }

    s_Running.store(0, std::memory_order_release);
    s_ShouldShutdown.store(0, std::memory_order_release);

    PlatformShutdown();

    ProcessEvents();
}

void
FileIo::ProcessEvents()
{
    ReadRequest* completedRequests = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        std::swap(s_Complete, completedRequests);
    }

    while(completedRequests)
    {
        ReadRequest* req = completedRequests;
        completedRequests = completedRequests->m_Next;

        FileIo::ReadResult result = req->Error.empty()
                                        ? FileIo::ReadResult(req->Buffer, req->BytesRead)
                                        : FileIo::ReadResult(std::string_view(req->Error));

        req->Callback(result, req->UserData);

        delete req;
    }

    if(s_ShouldShutdown.load(std::memory_order_acquire))
    {
        Shutdown();
    }
}

#if defined(_WIN32) && !defined(__EMSCRIPTEN__)

#include <iostream>
#include <thread>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>

static HANDLE s_IOCP = nullptr;
std::thread s_IocpWorker;

static std::string GetWindowsErrorString(DWORD errorCode);

static void WorkerLoop();

struct Win32ReadRequest : ReadRequest
{
    using ReadRequest::ReadRequest;

    ~Win32ReadRequest() override
    {
        if(File != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(File);
            File = INVALID_HANDLE_VALUE;
        }

        delete[] Buffer;
    }

    HANDLE File{ INVALID_HANDLE_VALUE };
    OVERLAPPED Ov{};
};

bool
FileIo::PlatformStartup()
{
    s_IOCP = ::CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
    if(s_IOCP == nullptr)
    {
        return false;
    }

    s_IocpWorker = std::thread(WorkerLoop);

    return true;
}

bool
FileIo::PlatformShutdown()
{
    ReadRequest* pending = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        pending = s_Pending;
        s_Pending = nullptr;
    }

    if(nullptr != s_IOCP)
    {
        // Wake worker.
        ::PostQueuedCompletionStatus(s_IOCP, 0, 0, nullptr);
    }

    if(s_IocpWorker.joinable())
    {
        s_IocpWorker.join();
    }

    // Cancel any pending IO
    std::size_t pendingCount = 0;
    for(ReadRequest* req = pending; req != nullptr; req = req->m_Next)
    {
        auto win32Req = req->As<Win32ReadRequest>();

        if(win32Req->File != INVALID_HANDLE_VALUE)
        {
            ::CancelIoEx(win32Req->File, &win32Req->Ov);
        }

        ++pendingCount;
    }

    // Process any remaining completions
    while(pendingCount > 0 && s_IOCP != nullptr)
    {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;

        const BOOL ok = ::GetQueuedCompletionStatus(s_IOCP, &bytes, &key, &pov, INFINITE);

        if(pov == nullptr && key == 0)
        {
            continue;
        }

        Win32ReadRequest* request = reinterpret_cast<Win32ReadRequest*>(key);
        if(request == nullptr)
        {
            continue;
        }

        if(!ok)
        {
            if(request->Error.empty())
            {
                request->Error = "Async read cancelled due to FileIO shutdown";
            }
        }

        request->BytesRead = static_cast<std::size_t>(bytes);
        request->IsComplete.store(1, std::memory_order_release);

        AddCompletedRequest(request);
        --pendingCount;
    }

    if(s_IOCP != nullptr)
    {
        ::CloseHandle(s_IOCP);
        s_IOCP = nullptr;
    }

    return true;
}

void
FileIo::QueueRead(std::string_view filePath, ReadCallback callback, void* userData)
{
    Win32ReadRequest* req = new Win32ReadRequest(std::string(filePath), userData, callback);

    if(!s_Running.load(std::memory_order_acquire) ||
        s_ShouldShutdown.load(std::memory_order_acquire))
    {
        req->Error = std::format("FileIO is not running or is shutting down.");
        req->IsComplete.store(1, std::memory_order_release);

        AddCompletedRequest(req);
        return;
    }

    // Open file
    req->File = ::CreateFileA(req->Path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_FLAG_OVERLAPPED | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);

    if(req->File == INVALID_HANDLE_VALUE)
    {
        // Failed to open file; fulfill promise with 0 bytes read.
        req->Error = std::format("Failed to open file: {}, error: {}",
            req->Path,
            GetWindowsErrorString(::GetLastError()));
        req->IsComplete.store(1, std::memory_order_release);

        AddCompletedRequest(req);
        return;
    }

    LARGE_INTEGER fsize;
    if(!::GetFileSizeEx(req->File, &fsize))
    {
        req->Error = std::format("Failed to get file size: {}, error: {}",
            req->Path,
            GetWindowsErrorString(::GetLastError()));
        req->IsComplete.store(1, std::memory_order_release);

        AddCompletedRequest(req);
        return;
    }

    const int64_t fileSize = fsize.QuadPart;

    if(fileSize < 0)
    {
        req->Error = std::format("Failed to get file size for {}.", req->Path);
        req->IsComplete.store(1, std::memory_order_release);

        AddCompletedRequest(req);
        return;
    }

    if(fileSize > std::numeric_limits<DWORD>::max())
    {
        req->Error = std::format("File is too large to read: {}", req->Path);
        req->IsComplete.store(1, std::memory_order_release);

        AddCompletedRequest(req);
        return;
    }

    if(!fileSize)
    {
        req->Error = std::format("File is empty: {}", req->Path);
        req->IsComplete.store(1, std::memory_order_release);

        AddCompletedRequest(req);
        return;
    }

    // Used as key to identify the request on completion.
    ULONG_PTR key = reinterpret_cast<ULONG_PTR>(req);

    // Bind file to IOCP.
    if(::CreateIoCompletionPort(req->File, s_IOCP, key, 0) == nullptr)
    {
        req->Error = std::format("Failed to bind file to IOCP: {}, error: {}",
            req->Path,
            GetWindowsErrorString(::GetLastError()));

        req->IsComplete.store(1, std::memory_order_release);

        AddCompletedRequest(req);
        return;
    }

    uint8_t* buffer = new uint8_t[fileSize];
    if(!buffer)
    {
        req->Error = std::format("Failed to allocate read buffer for file: {}", req->Path);
        req->IsComplete.store(1, std::memory_order_release);

        AddCompletedRequest(req);
        return;
    }

    req->Buffer = buffer;

    // Issue read
    DWORD bytesRead = 0;
    const BOOL ok =
        ::ReadFile(req->File, buffer, static_cast<DWORD>(fileSize), &bytesRead, &req->Ov);

    if(!ok)
    {
        const DWORD err = ::GetLastError();
        if(err != ERROR_IO_PENDING)
        {
            req->Error = std::format("Failed to issue read for file: {}, error: {}",
                req->Path,
                GetWindowsErrorString(err));
            req->IsComplete.store(1, std::memory_order_release);

            AddCompletedRequest(req);
            return;
        }
    }
    else
    {
        // Read completed immediately (rare). Fulfill promise now.
        // This is the other case where we can still use reqPtr.
        req->BytesRead = static_cast<std::size_t>(bytesRead);
        req->IsComplete.store(1, std::memory_order_release);

        AddCompletedRequest(req);
        return;
    }

    // Pending read; will be completed by worker thread.
    AddPendingRequest(req);
}

// private:

static void
WorkerLoop()
{
    while(s_Running.load(std::memory_order_acquire) &&
          !s_ShouldShutdown.load(std::memory_order_acquire))
    {
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        LPOVERLAPPED pov = nullptr;

        const BOOL ok = ::GetQueuedCompletionStatus(s_IOCP, &bytes, &key, &pov, INFINITE);

        if(!ok)
        {
            std::cout << std::format("GetQueuedCompletionStatus failed: {}",
                GetWindowsErrorString(::GetLastError()));

            // Signal shutdown due to error.
            s_ShouldShutdown.store(1, std::memory_order_release);

            continue;
        }

        if(pov == nullptr && key == 0)
        {
            // Received shutdown signal
            continue;
        }

        Win32ReadRequest* request = reinterpret_cast<Win32ReadRequest*>(key);
        if(request == nullptr)
        {
            std::cout << "WorkerLoop: completed request is null\n";
            continue;
        }

        if(request->File != INVALID_HANDLE_VALUE)
        {
            ::CloseHandle(request->File);
            request->File = INVALID_HANDLE_VALUE;
        }

        request->BytesRead = static_cast<std::size_t>(bytes);
        request->IsComplete.store(1, std::memory_order_release);

        AddCompletedRequest(request);
    }
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

#elif defined(__EMSCRIPTEN__)

#include <emscripten/emscripten.h>
#include <emscripten/fetch.h>

struct EmscriptenReadRequest : ReadRequest
{
    using ReadRequest::ReadRequest;

    ~EmscriptenReadRequest() override
    {
        if(FetchData)
        {
            emscripten_fetch_close(FetchData);
        }
    }

    emscripten_fetch_t* FetchData{ nullptr };
};

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
        pending = s_Pending;
        s_Pending = nullptr;
    }

    while(pending)
    {
        ReadRequest* req = pending;
        pending = pending->m_Next;

        auto* emReq = req->As<EmscriptenReadRequest>();

        if(emReq->FetchData)
        {
            emscripten_fetch_close(emReq->FetchData);
            emReq->FetchData = nullptr;
        }

        if(emReq->IsComplete.exchange(1, std::memory_order_acq_rel) == 1)
        {
            continue;
        }

        emReq->Error = "Async read cancelled due to FileIO shutdown";
        AddCompletedRequest(emReq);
    }

    return true;
}

void
FileIo::QueueRead(std::string_view filePath, ReadCallback callback, void* userData)
{
    EmscriptenReadRequest* reqPtr = new EmscriptenReadRequest(filePath, userData, callback);

    if(!s_Running.load(std::memory_order_acquire))
    {
        reqPtr->Error = std::format("Startup() was not called before QueueRead().");
        reqPtr->IsComplete.store(1, std::memory_order_release);

        AddCompletedRequest(reqPtr);
        return;
    }

    emscripten_fetch_attr_t attr{};
    emscripten_fetch_attr_init(&attr);

    std::strcpy(attr.requestMethod, "GET");
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.userData = reqPtr;

    attr.onsuccess = [](emscripten_fetch_t* fetch)
    {
        auto* req = static_cast<EmscriptenReadRequest*>(fetch->userData);

        // Check if already completed (e.g., due to shutdown)
        if(req->IsComplete.exchange(1, std::memory_order_acq_rel) == 1)
        {
            return;
        }

        req->Buffer = reinterpret_cast<const uint8_t*>(fetch->data);
        req->BytesRead = static_cast<std::size_t>(fetch->numBytes);
        req->FetchData = fetch;

        req->IsComplete.store(1, std::memory_order_release);

        AddCompletedRequest(req);
    };
    attr.onerror = [](emscripten_fetch_t* fetch)
    {
        auto* req = static_cast<EmscriptenReadRequest*>(fetch->userData);

        // Check if already completed (e.g., due to shutdown)
        if(req->IsComplete.exchange(1, std::memory_order_acq_rel) == 1)
        {
            return;
        }

        req->Error = std::format("Failed to fetch file: {}, status: {}/{}",
            fetch->url,
            fetch->status,
            fetch->statusText);

        req->FetchData = fetch;

        AddCompletedRequest(req);
    };

    reqPtr->FetchData = emscripten_fetch(&attr, reqPtr->Path.c_str());

    // Pending fetch; will be completed by fetch callbacks.
    AddPendingRequest(reqPtr);
}

#endif // _WIN32 && !__EMSCRIPTEN__