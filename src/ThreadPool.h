#pragma once

#include "Result.h"

#include <array>

/// @brief A simple thread pool for executing jobs asynchronously.
class ThreadPool final
{
public:
    ThreadPool() = delete;
    ~ThreadPool() = delete;
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;
    
    static Result<> Startup();

    static void Shutdown();

    static bool Enqueue(void (*jobFunc)(void*), void* userData);

    template<auto JobFunc, typename T>
    static bool Enqueue(T* userData)
    {
        auto wrapperFunc = [](void* data)
        {
            JobFunc(static_cast<T*>(data));
        };

        return Enqueue(wrapperFunc, userData);
    }

    static size_t GetWorkerCount();

private:
    struct Job
    {
        Job() = default;
        ~Job()
        {
            MLG_ASSERT(m_Next == nullptr);
        }
        Job(const Job&) = delete;
        Job& operator=(const Job&) = delete;
        Job(Job&&) = delete;
        Job& operator=(Job&&) = delete;

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
    static std::array<Job, kMaxJobs> s_JobPool;
    static Job* s_JobPoolFreeList;

    static Job *s_JobQueueHead;
    static Job *s_JobQueueTail;
};
