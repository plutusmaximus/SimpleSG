#pragma once

#include <utility>
#include <functional>

template<typename F, typename... Args>
class AutoDeleter
{
public:
    AutoDeleter(F&& f, Args&&... args)
        : m_Deleter(make_deleter(std::forward<F>(f), std::forward<Args>(args)...))
    {
    }

    // Non-copyable
    AutoDeleter(const AutoDeleter&) = delete;
    AutoDeleter& operator=(const AutoDeleter&) = delete;

    // Not moveable
    AutoDeleter(AutoDeleter&& other) = delete;
    AutoDeleter& operator=(AutoDeleter&&) = delete;

    ~AutoDeleter()
    {
        m_Deleter();
    }

private:

    static auto make_deleter(F&& f, Args&&... args)
    {
        return [f = std::forward<F>(f), ...args = std::forward<Args>(args)]() mutable
        {
            std::invoke(f, args...);
        };
    }

    using DeleterType = decltype(make_deleter(std::declval<F>(), std::declval<Args>()...));

    DeleterType m_Deleter;
};

// CTAD to help compiler deduce template args.
template<typename F, typename... Args>
AutoDeleter(F&&, Args&&...) -> AutoDeleter<F, Args...>;