#pragma once

#include <cstddef>
#include <cstdint>

struct ThreadPoolImpl;
struct ThreadPoolJob;

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

    static constexpr size_t kSizeofImplStorage = 25056;

    uint8_t m_ImplStorage[kSizeofImplStorage]{};
    ThreadPoolImpl* m_Impl{static_cast<ThreadPoolImpl*>(static_cast<void*>(m_ImplStorage))}; // NOLINT(bugprone-casting-through-void)
};
