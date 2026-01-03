#pragma once

#include <type_traits>

/// @brief A scope guard that executes a provided callable when it goes out of scope, unless cancelled.
template<typename F>
requires std::is_invocable_v<F>
class Finally
{
public:
    Finally(F&& f)
        : m_FinallyFunc(std::forward<F>(f))
    {
    }

    ~Finally()
    {
        if (!m_Cancelled)
        {
            m_FinallyFunc();
        }
    }

    void Cancel()
    {
        m_Cancelled = true;
    }

    // Non-copyable
    Finally(const Finally&) = delete;
    Finally& operator=(const Finally&) = delete;

    // Not moveable
    Finally(Finally&& other) = delete;
    Finally& operator=(Finally&&) = delete;

private:
    F m_FinallyFunc;
    bool m_Cancelled = false;
};