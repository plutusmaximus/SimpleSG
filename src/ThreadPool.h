#pragma once

#include "PoolAllocator.h"
#include "Result.h"

/// @brief A simple thread pool for executing jobs asynchronously.
class ThreadPool final
{
public:
    static void Startup();

    static void Shutdown();

    static bool Enqueue(void (*jobFunc)(void*), void* userData);

private:
    struct Job
    {
        Job() = default;
        Job(const Job&) = delete;
        Job& operator=(const Job&) = delete;
        Job(Job&&) = delete;
        Job& operator=(Job&&) = delete;

        ~Job()
        {
            MLG_ASSERT(m_Next == nullptr);
        }

        void Invoke()
        {
            MLG_ASSERT(m_JobFunc != nullptr);
            m_JobFunc(m_UserData);
        }

        Job *m_Next{ nullptr };

        void (*m_JobFunc)(void*){ nullptr };
        void* m_UserData{ nullptr };
    };

    static Job *NewJob();
    static void DeleteJob(Job *job);

    // Enqueue a new job. Returns false if the pool is stopping or not accepting work.
    static bool Enqueue(Job *job);

    static void WorkerLoop();

    static constexpr const size_t kMaxJobs = 1024;
    using PoolAllocatorType = PoolAllocator<Job, kMaxJobs>;
    static PoolAllocatorType s_JobAllocator;

    static Job *s_JobQueueHead;
    static Job *s_JobQueueTail;
};
