#pragma once
#include <version>

#if defined(__cpp_lib_scope_exit) && __cpp_lib_scope_exit >= 202011L
#include <scope>

template<class F>
using scope_exit = std::scope_exit<F>;

#else
#include <concepts>
#include <functional>
#include <type_traits>
#include <utility>

template<typename F>
concept IsCallableWithNoArgs = std::invocable<F>;

template<typename F>
concept ReturnsVoid = std::same_as<std::invoke_result_t<F>, void>;

template<typename F>
concept CleanupFunc = IsCallableWithNoArgs<F> && ReturnsVoid<F>;

/// @brief A scope guard that executes a provided callable when it goes out of scope.
/// This is a replacement for std::scope_exit in case it's not available.
/// As of MSVC 2022, std::scope_exit is not available.
template<CleanupFunc F>
class scope_exit
{
private:
    // Always hold a value type (no references) to keep lifetime independent.
    using StoredF = std::decay_t<F>;

public:
    template<typename U>
    // Perfect-forward into the stored value: copies lvalues, moves rvalues.
    explicit scope_exit(U&& f) noexcept(std::is_nothrow_constructible_v<StoredF, U>)
        requires std::constructible_from<StoredF, U>
        : m_Fn(std::forward<U>(f))
    {
    }

    scope_exit(scope_exit&& other) noexcept
        : m_Fn(std::move(other.m_Fn)),
          m_Active(other.m_Active)
    {
        other.m_Active = false;
    }

    scope_exit(const scope_exit&) = delete;
    scope_exit& operator=(const scope_exit&) = delete;
    scope_exit& operator=(scope_exit&&) = delete;

    ~scope_exit() noexcept
    {
        if(m_Active)
        {
            m_Fn();
        }
    }

    void release() noexcept { m_Active = false; }

private:
    StoredF m_Fn;
    bool m_Active{ true };
};

template<CleanupFunc F>
scope_exit(F) -> scope_exit<F>;

#endif