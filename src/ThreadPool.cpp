#include "ThreadPool.h"

#include "AssertHelper.h"

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
struct ThreadPoolJob
{
    ThreadPoolJob() = default;
    ~ThreadPoolJob() { MLG_ASSERT(m_Next == nullptr); }
    ThreadPoolJob(const ThreadPoolJob&) = delete;
    ThreadPoolJob& operator=(const ThreadPoolJob&) = delete;
    ThreadPoolJob(ThreadPoolJob&&) = delete;
    ThreadPoolJob& operator=(ThreadPoolJob&&) = delete;

    void Invoke() const
    {
        MLG_ASSERT(m_JobFunc != nullptr);
        m_JobFunc(m_UserData);
    }

    void Clear()
    {
        MLG_ASSERT(nullptr == m_Next, "Cannot clear a job that is still in a list!");
        m_JobFunc = nullptr;
        m_UserData = nullptr;
    }

    ThreadPoolJob* m_Next{ nullptr };

    void (*m_JobFunc)(void*){ nullptr };
    void* m_UserData{ nullptr };
};    
} // namespace

namespace mlg::detail
{
constexpr const size_t kMaxJobs = 1024;
constexpr size_t kMaxWorkerThreads = 32;

struct ThreadPoolImpl
{
    ThreadPoolJob *NewJob();
    
    void DeleteJob(ThreadPoolJob *job);

    void Enqueue(ThreadPoolJob *job);

    static size_t GetWorkerThreadCount();

    static void WorkerLoop(ThreadPoolImpl* impl);

    std::array<ThreadPoolJob, kMaxJobs> m_JobPool;
    ThreadPoolJob* m_JobPoolFreeList{nullptr};

    ThreadPoolJob *m_JobQueueHead{nullptr};
    ThreadPoolJob *m_JobQueueTail{nullptr};

    std::mutex m_JobQueueMutex;
    std::mutex m_AllocMutex;
    std::condition_variable m_ThreadPoolCv;
    std::atomic<bool> m_Running{false};
    std::array<std::thread, kMaxWorkerThreads> m_WorkerThreadPool;
    std::span<std::thread> m_WorkerThreads;
};

size_t
ThreadPoolImpl::GetWorkerThreadCount()
{
    const size_t hardwareThreadCount = std::thread::hardware_concurrency();
    if (hardwareThreadCount == 0)
    {
        return size_t{4};
    }

    return hardwareThreadCount > kMaxWorkerThreads ? kMaxWorkerThreads : hardwareThreadCount;
}
} // namespace mlg::detail

using namespace mlg::detail;

ThreadPool::ThreadPool()
{
    auto impl = std::make_unique<ThreadPoolImpl>();

    impl->m_WorkerThreads = std::span<std::thread>(impl->m_WorkerThreadPool.data(),
        ThreadPoolImpl::GetWorkerThreadCount());

    MLG_INFO("Starting ThreadPool with {} worker threads...", impl->m_WorkerThreads.size());

    impl->m_Running.store(true);

    for(std::thread& worker : impl->m_WorkerThreads)
    {
        worker = std::thread(ThreadPoolImpl::WorkerLoop, impl.get());
    }

    for(auto& job : impl->m_JobPool)
    {
        job.m_Next = impl->m_JobPoolFreeList;
        impl->m_JobPoolFreeList = &job;
    }

    m_Impl.reset(impl.release());
}

ThreadPool::~ThreadPool()
{
    if(!m_Impl)
    {
        return;
    }
    
    m_Impl->m_Running.store(false);

    ThreadPoolJob *pendingJobs = nullptr;
    {
        const std::lock_guard<std::mutex> lock(m_Impl->m_JobQueueMutex);

        std::swap(pendingJobs, m_Impl->m_JobQueueHead);
        m_Impl->m_JobQueueTail = nullptr;

        // Release all workers.
        m_Impl->m_ThreadPoolCv.notify_all();
    }

    MLG_ASSERT(!pendingJobs, "ThreadPool is being destroyed with pending jobs in the queue");

    for(std::thread& worker : m_Impl->m_WorkerThreads)
    {
        if(MLG_VERIFY(worker.joinable(), "Worker thread is not joinable"))
        {
            worker.join();
        }
    }

    while(pendingJobs)
    {
        ThreadPoolJob *job = pendingJobs;
        pendingJobs = pendingJobs->m_Next;
        job->m_Next = nullptr;

        m_Impl->DeleteJob(job);
    }

    while(m_Impl->m_JobPoolFreeList)
    {
        ThreadPoolJob *job = m_Impl->m_JobPoolFreeList;
        m_Impl->m_JobPoolFreeList = job->m_Next;
        job->m_Next = nullptr;
    }
}

void
ThreadPool::Deleter(mlg::detail::ThreadPoolImpl* impl)
{
    const std::unique_ptr<mlg::detail::ThreadPoolImpl> bye(impl);
}

bool
ThreadPool::Enqueue(void (*jobFunc)(void*), void* userData)
{
    ThreadPoolJob *job = m_Impl->NewJob();

    if(!MLG_VERIFY(job, "Failed to allocate job for ThreadPool.  Max jobs: {}", kMaxJobs))
    {
        return false;
    }

    job->m_JobFunc = jobFunc;
    job->m_UserData = userData;

    m_Impl->Enqueue(job);

    return true;
}

size_t
ThreadPool::GetWorkerCount() const
{
    return m_Impl->m_WorkerThreads.size();
}

ThreadPoolJob *
ThreadPoolImpl::NewJob()
{
    const std::lock_guard<std::mutex> lock(m_AllocMutex);

    ThreadPoolJob* job = m_JobPoolFreeList;
    if(MLG_VERIFY(job, "Failed to allocate job for ThreadPool.  Max jobs: {}", kMaxJobs))
    {
        m_JobPoolFreeList = job->m_Next;
        job->m_Next = nullptr;
    }

    return job;
}

void
ThreadPoolImpl::DeleteJob(ThreadPoolJob *job)
{
    const std::lock_guard<std::mutex> lock(m_AllocMutex);

    job->Clear();

    job->m_Next = m_JobPoolFreeList;
    m_JobPoolFreeList = job;
}

void
ThreadPoolImpl::Enqueue(ThreadPoolJob *job)
{
    const std::lock_guard<std::mutex> lock(m_JobQueueMutex);

    MLG_ASSERT(job->m_Next == nullptr);

    if(m_JobQueueTail)
    {
        m_JobQueueTail->m_Next = job;
        m_JobQueueTail = job;
    }
    else
    {
        m_JobQueueHead = job;
        m_JobQueueTail = job;
    }

    m_ThreadPoolCv.notify_one();
}

void
ThreadPoolImpl::WorkerLoop(ThreadPoolImpl* impl)
{
    while(impl->m_Running.load())
    {
        ThreadPoolJob *job = nullptr;

        std::unique_lock<std::mutex> lock(impl->m_JobQueueMutex);
        impl->m_ThreadPoolCv.wait(lock,
            [impl]
            { return !impl->m_Running.load() || impl->m_JobQueueHead != nullptr; });

        job = impl->m_JobQueueHead;
        if(!job)
        {
            continue;
        }

        impl->m_JobQueueHead = job->m_Next;

        if(!impl->m_JobQueueHead)
        {
            impl->m_JobQueueTail = nullptr;
        }

        job->m_Next = nullptr;

        lock.unlock();

        job->Invoke();

        impl->DeleteJob(job);
    }
}
