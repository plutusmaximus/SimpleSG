#pragma once

#include "AssertHelper.h"

#include <array>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <span>
#include <thread>

/// @brief A simple thread pool for executing jobs asynchronously.
class ThreadPool final
{
public:
    ThreadPool();
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    bool Enqueue(void (*jobFunc)(void*), void* userData);

    template<auto JobFunc, typename T>
    bool Enqueue(T* userData)
    {
        auto wrapperFunc = [](void* data)
        {
            JobFunc(static_cast<T*>(data));
        };

        return Enqueue(wrapperFunc, userData);
    }

    size_t GetWorkerCount() const;

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

    Job *NewJob();
    void DeleteJob(Job *job);

    void Enqueue(Job *job);

    static size_t GetWorkerThreadCount();

    static void WorkerLoop(ThreadPool* threadPool);

    static constexpr const size_t kMaxJobs = 1024;
    static constexpr size_t kMaxWorkerThreads = 32;

    std::array<Job, kMaxJobs> m_JobPool;
    Job* m_JobPoolFreeList{nullptr};

    Job *m_JobQueueHead{nullptr};
    Job *m_JobQueueTail{nullptr};

    std::mutex m_JobQueueMutex;
    std::mutex m_AllocMutex;
    std::condition_variable m_ThreadPoolCv;
    std::atomic<bool> m_Running{false};
    std::array<std::thread, kMaxWorkerThreads> m_WorkerThreadPool;
    std::span<std::thread> m_WorkerThreads;
};
