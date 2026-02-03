#include "ThreadPool.h"

#include <atomic>
#include <cassert>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <list>
#include <mutex>
#include <semaphore>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

static std::mutex s_Mutex;
static std::condition_variable s_Cv;
static ThreadPool::JobWrapper *s_JobQueueHead{ nullptr };
static ThreadPool::JobWrapper *s_JobQueueTail{ nullptr };
static std::vector<std::thread> s_Workers;
static std::list<std::vector<ThreadPool::JobWrapper>> s_JobWrapperHeaps;
static ThreadPool::JobWrapper *s_JobWrapperPool;

static std::atomic<bool> s_Running{ false };
static const std::size_t kDefaultThreadCount = std::thread::hardware_concurrency() > 0
                                                   ? std::thread::hardware_concurrency()
                                                   : 4;

// Ensure ThreadPool is start/stopped automatically.
static struct ThreadPoolStartStop
{
    ThreadPoolStartStop() { ThreadPool::Startup(); }
    ~ThreadPoolStartStop() { ThreadPool::Shutdown(); }
} s_ThreadPoolStopper;

void
ThreadPool::JobWrapper::InvokeJob()
{
    Job *job = reinterpret_cast<Job *>(m_CallableBuf);
    job->Invoke();
    job->~Job();
}

void
ThreadPool::Startup()
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    bool expected = false;
    if(!s_Running.compare_exchange_strong(expected, true))
    {
        return; // already running
    }

    s_Workers.reserve(kDefaultThreadCount);
    for(std::size_t i = 0; i < kDefaultThreadCount; ++i)
    {
        s_Workers.emplace_back(WorkerLoop);
    }
}

void
ThreadPool::Shutdown()
{
    std::vector<std::thread> oldWorkers;

    {
        std::lock_guard<std::mutex> lock(s_Mutex);

        bool expected = true;
        if(!s_Running.compare_exchange_strong(expected, false))
        {
            return; // already stopped
        }

        std::swap(s_Workers, oldWorkers);

        // Release all workers.
        s_Cv.notify_all();
    }

    for(auto &t : oldWorkers)
    {
        if(t.joinable())
        {
            t.join();
        }
    }

    ThreadPool::JobWrapper *pendingJobs = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_Mutex);
        std::swap(pendingJobs, s_JobQueueHead);
        s_JobQueueTail = nullptr;
    }

    while(pendingJobs)
    {
        ThreadPool::JobWrapper *jobWrapper = pendingJobs;
        pendingJobs = pendingJobs->m_Next;
        jobWrapper->m_Next = nullptr;

        Job *job = reinterpret_cast<Job *>(jobWrapper->m_CallableBuf);
        job->~Job();

        FreeJobWrapper(jobWrapper);
    }

    s_JobWrapperHeaps.clear();
    s_JobWrapperPool = nullptr;
}

ThreadPool::JobWrapper *
ThreadPool::AllocJobWrapper()
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    if(!s_Running.load())
    {
        return nullptr;
    }

    if(!s_JobWrapperPool)
    {
        constexpr std::size_t kHeapSize = 1024;
        s_JobWrapperHeaps.emplace_back();
        s_JobWrapperHeaps.back().resize(kHeapSize);
        for(std::size_t i = 0; i < kHeapSize - 1; ++i)
        {
            s_JobWrapperHeaps.back()[i].m_Next = &s_JobWrapperHeaps.back()[i + 1];
        }
        s_JobWrapperHeaps.back().back().m_Next = nullptr;
        s_JobWrapperPool = &s_JobWrapperHeaps.back().front();
    }

    if(!s_JobWrapperPool)
    {
        return nullptr;
    }

    ThreadPool::JobWrapper *jobWrapper = s_JobWrapperPool;
    s_JobWrapperPool = s_JobWrapperPool->m_Next;
    jobWrapper->m_Next = nullptr;
    return jobWrapper;
}

void
ThreadPool::FreeJobWrapper(ThreadPool::JobWrapper *jobWrapper)
{
    std::lock_guard<std::mutex> lock(s_Mutex);

    assert(jobWrapper->m_Next == nullptr);

    jobWrapper->m_Next = s_JobWrapperPool;
    s_JobWrapperPool = jobWrapper;
}

// Enqueue a new job. Returns false if the pool is stopping or not accepting work.
bool
ThreadPool::Enqueue(JobWrapper *job)
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
        ThreadPool::JobWrapper *job;

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
            job->InvokeJob();
        }
        catch(...)
        {
            // Swallow exceptions to keep worker thread alive.
        }

        FreeJobWrapper(job);
    }
}
