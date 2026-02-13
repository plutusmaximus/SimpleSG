#pragma once

#include "PoolAllocator.h"
#include "Result.h"

#include <concepts>
#include <cstddef>
#include <functional>
#include <type_traits>
#include <utility>

/// @brief Concept for types that can be used as jobs in the thread pool.
template<typename F>
concept JobLike = std::invocable<F> && std::same_as<std::invoke_result_t<F>, void>;

/// @brief A simple thread pool for executing jobs asynchronously.
class ThreadPool final
{
public:
    static void Startup();

    static void Shutdown();

    template<JobLike JF>
    static bool Enqueue(JF &&jobFunc)
    {
        // Store decayed-by-value callables so lvalues are copied and rvalues moved,
        // avoiding dangling references to caller-owned lambdas.
        using JFunc = std::decay_t<JF>;
        static_assert(sizeof(JFunc) <= sizeof(Job::m_CallableBuf),
            "JobWrapper buffer too small for job function object");

        Job *job = NewJob();
        if(!job)
        {
            return false;
        }

        ::new (job->m_CallableBuf) JFunc{ std::forward<JF>(jobFunc) };

        job->InvokeCb = [](void *buf)
        { std::invoke(*reinterpret_cast<JFunc *>(buf)); };
        job->DestroyCb = [](void *buf)
        { reinterpret_cast<JFunc *>(buf)->~JFunc(); };

        if(!Enqueue(job))
        {
            DeleteJob(job);
            return false;
        }

        return true;
    }

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
            eassert(m_Next == nullptr);

            if(DestroyCb)
            {
                DestroyCb(m_CallableBuf);
            }
        }

        void Invoke()
        {
            eassert(InvokeCb != nullptr);
            InvokeCb(m_CallableBuf);
        }

        Job *m_Next{ nullptr };

        void (*InvokeCb)(void *buf){ nullptr };

        void (*DestroyCb)(void *buf){ nullptr };

        alignas(std::max_align_t) unsigned char m_CallableBuf[32];
    };

    static Job *NewJob();
    static void DeleteJob(Job *job);

    // Enqueue a new job. Returns false if the pool is stopping or not accepting work.
    static bool Enqueue(Job *job);

    static void WorkerLoop();

    using PoolAllocatorType = PoolAllocator<Job, 128>;
    static PoolAllocatorType s_JobAllocator;

    static Job *s_JobQueueHead;
    static Job *s_JobQueueTail;
};
