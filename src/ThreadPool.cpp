#include "ThreadPool.h"

#include "PoolAllocator.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <thread>
#include <utility>

// Mutex to protect access to the job queue.
static std::mutex s_Mutex;
// Mutex to protect job allocation/freeing.
static std::mutex s_AllocMutex;
// Condition variable to notify worker threads of new jobs.
static std::condition_variable s_Cv;
// Head and tail of the job queue.  Implements a FIFO queue of jobs.
ThreadPool::Job *ThreadPool::s_JobQueueHead{ nullptr };
ThreadPool::Job *ThreadPool::s_JobQueueTail{ nullptr };

ThreadPool::PoolAllocatorType ThreadPool::s_JobAllocator;

static std::atomic<bool> s_Running{ false };

static constexpr std::size_t MAX_WORKER_THREADS = 32;
static std::thread s_WorkerThreads[MAX_WORKER_THREADS];

static const std::size_t kDefaultThreadCount = std::thread::hardware_concurrency() > 0
                                                   ? (std::thread::hardware_concurrency() >
                                                                 MAX_WORKER_THREADS
                                                             ? MAX_WORKER_THREADS
                                                             : std::thread::hardware_concurrency())
                                                   : 4;

// Ensure ThreadPool is start/stopped automatically.
static struct ThreadPoolStartStop
{
    ThreadPoolStartStop() { ThreadPool::Startup(); }
    ~ThreadPoolStartStop() { ThreadPool::Shutdown(); }
} s_ThreadPoolStopper;

void
ThreadPool::Startup()
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    bool expected = false;
    if(!s_Running.compare_exchange_strong(expected, true))
    {
        return; // already running
    }

    for(std::size_t i = 0; i < kDefaultThreadCount; ++i)
    {
        s_WorkerThreads[i] = std::thread(WorkerLoop);
    }
}

void
ThreadPool::Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(s_Mutex);

        bool expected = true;
        if(!s_Running.compare_exchange_strong(expected, false))
        {
            return; // already stopped
        }

        // Release all workers.
        s_Cv.notify_all();
    }

    for(std::size_t i = 0; i < kDefaultThreadCount; ++i)
    {
        if(s_WorkerThreads[i].joinable())
        {
            s_WorkerThreads[i].join();
        }
    }

    ThreadPool::Job *pendingJobs = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        std::swap(pendingJobs, s_JobQueueHead);
        s_JobQueueTail = nullptr;
    }

    while(pendingJobs)
    {
        ThreadPool::Job *job = pendingJobs;
        pendingJobs = pendingJobs->m_Next;
        job->m_Next = nullptr;

        FreeJob(job);
    }
}

ThreadPool::Job *
ThreadPool::AllocJob()
{
    std::lock_guard<std::mutex> lock(s_AllocMutex);

    if(!s_Running.load())
    {
        return nullptr;
    }

    return s_JobAllocator.Alloc();
}

void
ThreadPool::FreeJob(ThreadPool::Job *job)
{
    std::lock_guard<std::mutex> lock(s_AllocMutex);

    s_JobAllocator.Free(job);
}

// Enqueue a new job. Returns false if the pool is stopping or not accepting work.
bool
ThreadPool::Enqueue(Job *job)
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    assert(job->m_Next == nullptr);

    if(!s_Running.load())
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

    s_Cv.notify_one();

    return true;
}

void
ThreadPool::WorkerLoop()
{
    while(s_Running.load())
    {
        ThreadPool::Job *job;

        std::unique_lock<std::mutex> lock(s_Mutex);
        s_Cv.wait(lock, [] { return !s_Running.load() || s_JobQueueHead != nullptr; });

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

        try
        {
            job->Invoke();
        }
        catch(...)
        {
            // Swallow exceptions to keep worker thread alive.
        }

        FreeJob(job);
    }
}
