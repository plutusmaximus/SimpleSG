#pragma once

#include "Result.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <thread>

/// @brief A simple thread pool for executing jobs asynchronously.
class ThreadPool final
{
public:
    static constexpr const size_t kMaxJobs = 1024;
    static constexpr size_t kMaxWorkerThreads = 32;

    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&& other) = delete;
    ThreadPool& operator=(ThreadPool&& other) = delete;

    static Result<std::unique_ptr<ThreadPool>> Create();

    bool Enqueue(void (*jobFunc)(void*), void* userData);

    template<auto JobFunc, typename T>
    bool Enqueue(T* userData)
    {
        static_assert(std::is_invocable_v<decltype(JobFunc), T*>);

        auto wrapperFunc = [](void* data) { JobFunc(static_cast<T*>(data)); };

        return Enqueue(wrapperFunc, userData);
    }

    size_t GetWorkerCount() const;

private:
    struct Job
    {
        Job() = default;
        ~Job();
        Job(const Job&) = delete;
        Job& operator=(const Job&) = delete;
        Job(Job&&) = delete;
        Job& operator=(Job&&) = delete;

        void Invoke() const;

        void Clear();

        Job* m_Next{ nullptr };

        void (*m_JobFunc)(void*){ nullptr };
        void* m_UserData{ nullptr };
    };

    ThreadPool();

    static size_t GetWorkerThreadCount();

    static void WorkerLoop(ThreadPool* threadPool);

    Job* NewJob();

    void DeleteJob(Job* job);

    void Enqueue(Job* job);

    std::array<Job, kMaxJobs> m_JobPool;
    Job* m_JobPoolFreeList{ nullptr };

    Job* m_JobQueueHead{ nullptr };
    Job* m_JobQueueTail{ nullptr };

    std::mutex m_JobQueueMutex;
    std::mutex m_AllocMutex;
    std::condition_variable m_ThreadPoolCv;
    std::atomic<bool> m_Running{ false };
    std::array<std::thread, kMaxWorkerThreads> m_WorkerThreadPool;
    std::span<std::thread> m_WorkerThreads;
};
