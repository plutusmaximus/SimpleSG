#pragma once

#include <cstddef>
#include <cstdint>
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
    ThreadPool(ThreadPool&& other) noexcept;
    ThreadPool& operator=(ThreadPool&& other) noexcept;

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

    mlg::detail::ThreadPoolImpl* m_Impl{nullptr};
};
