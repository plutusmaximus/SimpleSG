#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>

/// @brief A simple thread pool for executing jobs asynchronously.
class ThreadPool final
{
    class Impl;
public:
    ThreadPool();
    ~ThreadPool();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&& other) = default;
    ThreadPool& operator=(ThreadPool&& other) = default;

    bool Enqueue(void (*jobFunc)(void*), void* userData);

    template<auto JobFunc, typename T>
    bool Enqueue(T* userData)
    {
        static_assert(std::is_invocable_v<decltype(JobFunc), T*>);
        
        auto wrapperFunc = [](void* data)
        {
            JobFunc(static_cast<T*>(data));
        };

        return Enqueue(wrapperFunc, userData);
    }

    size_t GetWorkerCount() const;

private:

    static void Deleter(Impl* impl);

    using DeleterType = decltype(&Deleter);
    using UniquePtrType = std::unique_ptr<Impl, DeleterType>;

    UniquePtrType m_Impl{nullptr, &ThreadPool::Deleter};
};
