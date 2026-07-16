#include "ThreadPool.h"

#include "AssertHelper.h"
#include "Log.h"

////////// ThreadPool::Job

ThreadPool::Job::~Job()
{
    MLG_ASSERT(m_Next == nullptr, "Job is being destroyed while still in a list!");
}

void
ThreadPool::Job::Invoke() const
{
    MLG_ASSERT(m_JobFunc != nullptr);
    m_JobFunc(m_UserData);
}

void
ThreadPool::Job::Clear()
{
    MLG_ASSERT(nullptr == m_Next, "Cannot clear a job that is still in a list!");
    m_JobFunc = nullptr;
    m_UserData = nullptr;
}

////////// ThreadPool::Impl

ThreadPool::Job*
ThreadPool::NewJob()
{
    const std::lock_guard<std::mutex> lock(m_AllocMutex);

    Job* job = m_JobPoolFreeList;
    if(MLG_VERIFY(job, "Failed to allocate job for ThreadPool.  Max jobs: {}", kMaxJobs))
    {
        m_JobPoolFreeList = job->m_Next;
        job->m_Next = nullptr;
    }

    return job;
}

void
ThreadPool::DeleteJob(Job* job)
{
    const std::lock_guard<std::mutex> lock(m_AllocMutex);

    job->Clear();

    job->m_Next = m_JobPoolFreeList;
    m_JobPoolFreeList = job;
}

void
ThreadPool::Enqueue(Job* job)
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

////////// ThreadPool

Result<std::unique_ptr<ThreadPool>>
ThreadPool::Create()
{
    return std::unique_ptr<ThreadPool>(new ThreadPool());
}

ThreadPool::ThreadPool()
{
    m_WorkerThreads =
        std::span<std::thread>(m_WorkerThreadPool.data(), GetWorkerThreadCount());

    MLG_INFO("Starting ThreadPool with {} worker threads...", m_WorkerThreads.size());

    m_Running.store(true);

    for(std::thread& worker : m_WorkerThreads)
    {
        worker = std::thread(WorkerLoop, this);
    }

    for(auto& job : m_JobPool)
    {
        job.m_Next = m_JobPoolFreeList;
        m_JobPoolFreeList = &job;
    }
}

ThreadPool::~ThreadPool()
{
    m_Running.store(false);

    Job* pendingJobs = nullptr;
    {
        const std::lock_guard<std::mutex> lock(m_JobQueueMutex);

        std::swap(pendingJobs, m_JobQueueHead);
        m_JobQueueTail = nullptr;

        // Release all workers.
        m_ThreadPoolCv.notify_all();
    }

    MLG_ASSERT(!pendingJobs, "ThreadPool is being destroyed with pending jobs in the queue");

    for(std::thread& worker : m_WorkerThreads)
    {
        if(MLG_VERIFY(worker.joinable(), "Worker thread is not joinable"))
        {
            worker.join();
        }
    }

    while(pendingJobs)
    {
        Job* job = pendingJobs;
        pendingJobs = pendingJobs->m_Next;
        job->m_Next = nullptr;

        DeleteJob(job);
    }

    while(m_JobPoolFreeList)
    {
        Job* job = m_JobPoolFreeList;
        m_JobPoolFreeList = job->m_Next;
        job->m_Next = nullptr;
    }
}

bool
ThreadPool::Enqueue(void (*jobFunc)(void*), void* userData)
{
    Job* job = NewJob();

    if(!MLG_VERIFY(job, "Failed to allocate job for ThreadPool.  Max jobs: {}", kMaxJobs))
    {
        return false;
    }

    job->m_JobFunc = jobFunc;
    job->m_UserData = userData;

    Enqueue(job);

    return true;
}

size_t
ThreadPool::GetWorkerCount() const
{
    return m_WorkerThreads.size();
}

size_t
ThreadPool::GetWorkerThreadCount()
{
    const size_t hardwareThreadCount = std::thread::hardware_concurrency();
    if(hardwareThreadCount == 0)
    {
        return size_t{ 4 };
    }

    return hardwareThreadCount > kMaxWorkerThreads ? kMaxWorkerThreads : hardwareThreadCount;
}

void
ThreadPool::WorkerLoop(ThreadPool* threadPool)
{
    while(threadPool->m_Running.load())
    {
        Job* job = nullptr;

        {
            //Scope lock for the job queue mutex.  This will be released when the lock goes out of scope.
            std::unique_lock<std::mutex> lock(threadPool->m_JobQueueMutex);

            threadPool->m_ThreadPoolCv.wait(lock,
                [threadPool] { return !threadPool->m_Running.load() || threadPool->m_JobQueueHead != nullptr; });

            job = threadPool->m_JobQueueHead;
            if(!job)
            {
                continue;
            }

            threadPool->m_JobQueueHead = job->m_Next;

            if(!threadPool->m_JobQueueHead)
            {
                threadPool->m_JobQueueTail = nullptr;
            }

            job->m_Next = nullptr;
        }

        job->Invoke();

        threadPool->DeleteJob(job);
    }
}
