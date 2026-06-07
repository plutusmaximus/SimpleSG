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

/// @brief A scope guard that executes a provided callable when it goes out of scope.
/// This is a replacement for std::scope_exit in case it's not available.
/// As of MSVC 2022, std::scope_exit is not available.
template<typename F>
requires std::invocable<F> && std::same_as<std::invoke_result_t<F>, void>
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

    scope_exit(const scope_exit&) = delete;
    scope_exit& operator=(const scope_exit&) = delete;
    scope_exit(scope_exit&& other) noexcept
        : m_Fn(std::move(other.m_Fn)),
          m_Active(other.m_Active)
    {
        other.release(); // Prevent the moved-from guard from running
    }
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

template<typename F>
requires std::invocable<F> && std::same_as<std::invoke_result_t<F>, void>
scope_exit(F) -> scope_exit<F>;

#endif

#define MLG_SCOPE_EXIT_CAT_1(a, b) a##b
#define MLG_SCOPE_EXIT_CAT(a, b) MLG_SCOPE_EXIT_CAT_1(a, b)

class MLG_DeferHelper
{
public:
    // Tricky operator+ allows us to write "defer + [&](){ ... }".
    // It basically enables the syntax of "defer { ... }".
    template<class F>
    friend auto operator+(MLG_DeferHelper, F&& f)
    {
        return scope_exit(std::forward<F>(f));
    }
};

#define MLG_DEFER \
    const auto MLG_SCOPE_EXIT_CAT(_defer_, __LINE__) = MLG_DeferHelper{} + [&]()

// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define MLG_DEFER_AS(name) auto name = MLG_DeferHelper{} + [&]()