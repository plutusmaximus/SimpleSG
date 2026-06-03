#include "ThreadPool.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <span>
#include <thread>
#include <utility>

namespace
{
constexpr size_t kMaxWorkerThreads = 32;

struct TPGlobals
{
    static inline std::mutex JobQueueMutex;
    static inline std::mutex AllocMutex;
    static inline std::condition_variable ThreadPoolCv;
    static inline std::atomic<bool> Running{false};
    static inline std::array<std::thread, kMaxWorkerThreads> WorkerThreadPool;
    static inline const size_t ThreadCount = []{
        const size_t hardwareThreadCount = std::thread::hardware_concurrency();
        if(hardwareThreadCount == 0)
        {
            return size_t{4};
        }

        return hardwareThreadCount > kMaxWorkerThreads ? kMaxWorkerThreads : hardwareThreadCount;
    }();
    static inline std::span<std::thread> WorkerThreads{WorkerThreadPool.data(), ThreadCount};

    static Result<> VerifyStarted()
    {
        MLG_CHECKV(Running.load(), "ThreadPool not initialized - call Startup()");

        return Result<>::Ok;
    }
};
} // namespace

std::array<ThreadPool::Job, ThreadPool::kMaxJobs> ThreadPool::s_JobPool;
ThreadPool::Job* ThreadPool::s_JobPoolFreeList{ nullptr };

// Head and tail of the FIFO job queue.
ThreadPool::Job *ThreadPool::s_JobQueueHead{ nullptr };
ThreadPool::Job *ThreadPool::s_JobQueueTail{ nullptr };

Result<>
ThreadPool::Startup()
{
    const std::lock_guard<std::mutex> lock(TPGlobals::JobQueueMutex);

    bool expected = false;
    if(!TPGlobals::Running.compare_exchange_strong(expected, true))
    {
        return Result<>::Ok; // already running
    }

    for(std::thread& worker : TPGlobals::WorkerThreads)
    {
        worker = std::thread(WorkerLoop);
    }

    for(auto& job : s_JobPool)
    {
        job.m_Next = s_JobPoolFreeList;
        s_JobPoolFreeList = &job;
    }

    return Result<>::Ok;
}

void
ThreadPool::Shutdown()
{
    {
        const std::lock_guard<std::mutex> lock(TPGlobals::JobQueueMutex);

        bool expected = true;
        if(!TPGlobals::Running.compare_exchange_strong(expected, false))
        {
            return; // already stopped
        }

        // Release all workers.
        TPGlobals::ThreadPoolCv.notify_all();
    }

    for(std::thread& worker : TPGlobals::WorkerThreads)
    {
        if(worker.joinable())
        {
            worker.join();
        }
    }

    ThreadPool::Job *pendingJobs = nullptr;
    {
        const std::lock_guard<std::mutex> lock(TPGlobals::JobQueueMutex);
        std::swap(pendingJobs, s_JobQueueHead);
        s_JobQueueTail = nullptr;
    }

    while(pendingJobs)
    {
        ThreadPool::Job *job = pendingJobs;
        pendingJobs = pendingJobs->m_Next;
        job->m_Next = nullptr;

        DeleteJob(job);
    }

    while(s_JobPoolFreeList)
    {
        ThreadPool::Job *job = s_JobPoolFreeList;
        s_JobPoolFreeList = job->m_Next;
        job->m_Next = nullptr;
    }
}

bool
ThreadPool::Enqueue(void (*jobFunc)(void*), void* userData)
{
    if(!TPGlobals::VerifyStarted())
    {
        return false;
    }

    Job *job = NewJob();

    if(!MLG_VERIFY(job, "Failed to allocate job for ThreadPool.  Max jobs: {}", kMaxJobs))
    {
        return false;
    }

    job->m_JobFunc = jobFunc;
    job->m_UserData = userData;

    if(!Enqueue(job))
    {
        DeleteJob(job);
        return false;
    }

    return true;
}

size_t
ThreadPool::GetWorkerCount()
{
    return TPGlobals::ThreadCount;
}

ThreadPool::Job *
ThreadPool::NewJob()
{
    const std::lock_guard<std::mutex> lock(TPGlobals::AllocMutex);

    if(!TPGlobals::VerifyStarted())
    {
        return nullptr;
    }

    Job* job = s_JobPoolFreeList;
    if(MLG_VERIFY(job, "Failed to allocate job for ThreadPool.  Max jobs: {}", kMaxJobs))
    {
        s_JobPoolFreeList = job->m_Next;
        job->m_Next = nullptr;
    }

    return job;
}

void
ThreadPool::DeleteJob(ThreadPool::Job *job)
{
    const std::lock_guard<std::mutex> lock(TPGlobals::AllocMutex);

    job->Clear();

    job->m_Next = s_JobPoolFreeList;
    s_JobPoolFreeList = job;
}

// Enqueue a new job. Returns false if the pool is stopping or not accepting work.
bool
ThreadPool::Enqueue(Job *job)
{
    const std::lock_guard<std::mutex> lock(TPGlobals::JobQueueMutex);

    MLG_ASSERT(job->m_Next == nullptr);

    if(!MLG_VERIFY(TPGlobals::Running.load(), "ThreadPool is not running"))
    {
        return false;
    }

    if(s_JobQueueTail)
    {
        s_JobQueueTail->m_Next = job;
        s_JobQueueTail = job;
    }
    else
    {
        s_JobQueueHead = job;
        s_JobQueueTail = job;
    }

    TPGlobals::ThreadPoolCv.notify_one();

    return true;
}

void
ThreadPool::WorkerLoop()
{
    while(TPGlobals::Running.load())
    {
        ThreadPool::Job *job = nullptr;

        std::unique_lock<std::mutex> lock(TPGlobals::JobQueueMutex);
        TPGlobals::ThreadPoolCv.wait(lock, [] { return !TPGlobals::Running.load() || s_JobQueueHead != nullptr; });

        job = s_JobQueueHead;
        if(!job)
        {
            continue;
        }

        s_JobQueueHead = s_JobQueueHead->m_Next;

        if(!s_JobQueueHead)
        {
            s_JobQueueTail = nullptr;
        }

        job->m_Next = nullptr;

        lock.unlock();

        job->Invoke();

        DeleteJob(job);
    }
}
