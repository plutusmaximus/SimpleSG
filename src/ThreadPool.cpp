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
ThreadPool::Impl::NewJob()
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
ThreadPool::Impl::DeleteJob(Job* job)
{
    const std::lock_guard<std::mutex> lock(m_AllocMutex);

    job->Clear();

    job->m_Next = m_JobPoolFreeList;
    m_JobPoolFreeList = job;
}

void
ThreadPool::Impl::Enqueue(Job* job)
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

Result<ThreadPool>
ThreadPool::Create()
{
    return ThreadPool();
}

ThreadPool::ThreadPool()
    : m_Impl(std::make_unique<Impl>())
{
    m_Impl->m_WorkerThreads =
        std::span<std::thread>(m_Impl->m_WorkerThreadPool.data(), GetWorkerThreadCount());

    MLG_INFO("Starting ThreadPool with {} worker threads...", m_Impl->m_WorkerThreads.size());

    m_Impl->m_Running.store(true);

    for(std::thread& worker : m_Impl->m_WorkerThreads)
    {
        worker = std::thread(WorkerLoop, m_Impl.get());
    }

    for(auto& job : m_Impl->m_JobPool)
    {
        job.m_Next = m_Impl->m_JobPoolFreeList;
        m_Impl->m_JobPoolFreeList = &job;
    }
}

ThreadPool::~ThreadPool()
{
    if(!m_Impl)
    {
        return;
    }

    m_Impl->m_Running.store(false);

    Job* pendingJobs = nullptr;
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
        Job* job = pendingJobs;
        pendingJobs = pendingJobs->m_Next;
        job->m_Next = nullptr;

        m_Impl->DeleteJob(job);
    }

    while(m_Impl->m_JobPoolFreeList)
    {
        Job* job = m_Impl->m_JobPoolFreeList;
        m_Impl->m_JobPoolFreeList = job->m_Next;
        job->m_Next = nullptr;
    }
}

bool
ThreadPool::Enqueue(void (*jobFunc)(void*), void* userData)
{
    if(!MLG_VERIFY(m_Impl, "ThreadPool is invalid."))
    {
        return false;
    }

    Job* job = m_Impl->NewJob();

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
    if(!MLG_VERIFY(m_Impl, "ThreadPool is invalid."))
    {
        return 0;
    }

    return m_Impl->m_WorkerThreads.size();
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
ThreadPool::WorkerLoop(Impl* impl)
{
    while(impl->m_Running.load())
    {
        Job* job = nullptr;

        {
            //Scope lock for the job queue mutex.  This will be released when the lock goes out of scope.
            std::unique_lock<std::mutex> lock(impl->m_JobQueueMutex);

            impl->m_ThreadPoolCv.wait(lock,
                [impl] { return !impl->m_Running.load() || impl->m_JobQueueHead != nullptr; });

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
        }

        job->Invoke();

        impl->DeleteJob(job);
    }
}
