#pragma once

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
    /// @brief A wrapper for a job to be executed by the thread pool.
    /// The buffer size must be large enough to hold any JobT instantiation,
    /// which includes the callable object (like a lambda) used as the job.
    struct JobWrapper
    {
        alignas(std::max_align_t) unsigned char m_CallableBuf[32];

        void InvokeJob();

        JobWrapper *m_Next{ nullptr };
    };

    static void Startup();

    static void Shutdown();

    template<JobLike JF>
    static bool Enqueue(JF &&jobFunc)
    {
        // Store decayed-by-value callables so lvalues are copied and rvalues moved,
        // avoiding dangling references to caller-owned lambdas.
        using JFunc = JobT<std::decay_t<JF>>;
        static_assert(sizeof(JFunc) <= sizeof(JobWrapper::m_CallableBuf),
            "JobWrapper buffer too small for job function object");

        JobWrapper *jobWrapper = AllocJobWrapper();
        if(!jobWrapper)
        {
            return false;
        }
        Job *job = ::new(static_cast<void *>(jobWrapper->m_CallableBuf))
            JFunc{ std::forward<JF>(jobFunc) };

        if(!Enqueue(jobWrapper))
        {
            job->~Job();
            FreeJobWrapper(jobWrapper);
            return false;
        }

        return true;
    }

    static void ProcessCompletions();

private:
    struct Job
    {
        virtual ~Job() = default;

        virtual void Invoke() = 0;
    };

    template<JobLike F>
    struct JobT : public Job
    {
        // Always hold a value type (no references) to keep lifetime independent.
        using StoredF = std::decay_t<F>;

        template<typename U>
            requires std::constructible_from<StoredF, U>
        // Perfect-forward into the stored value: copies lvalues, moves rvalues.
        explicit JobT(U &&f)
            : fn(std::forward<U>(f))
        {
        }

        void Invoke() override { std::invoke(fn); }
        StoredF fn;
    };

    static JobWrapper *AllocJobWrapper();
    static void FreeJobWrapper(ThreadPool::JobWrapper *jobWrapper);

    // Enqueue a new job. Returns false if the pool is stopping or not accepting work.
    static bool Enqueue(JobWrapper *job);

    static void WorkerLoop();
};
