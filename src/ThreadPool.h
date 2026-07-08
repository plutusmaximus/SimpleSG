#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>

namespace mlg::detail
{
struct ThreadPoolImpl;
}

/// @brief A simple thread pool for executing jobs asynchronously.
class ThreadPool final
{
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

    static void Deleter(mlg::detail::ThreadPoolImpl* impl);

    using DeleterType = decltype(&Deleter);
    using UniquePtrType = std::unique_ptr<mlg::detail::ThreadPoolImpl, DeleterType>;

    UniquePtrType m_Impl{nullptr, &ThreadPool::Deleter};
};
