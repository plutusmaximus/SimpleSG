#pragma once

#include <concepts>
#include <type_traits>
#include <utility>

/// A scope guard that executes a cleanup function when it goes out of scope.
template<typename F>
class Defer
{
    static_assert(std::invocable<F> && std::same_as<std::invoke_result_t<F>, void>,
        "Defer requires a callable that returns void");
private:
    // Always hold a value type for the function (no references) to keep lifetime independent.
    // Ensures captured callables are stored by value, avoiding dangling references.
    using StoredF = std::decay_t<F>;

public:
    template<typename U>
    explicit Defer(U&& f) noexcept(std::is_nothrow_constructible_v<StoredF, U>)
        requires std::constructible_from<StoredF, U>
        : m_Fn(std::forward<U>(f))
    {
    }

    Defer(const Defer&) = delete;
    Defer& operator=(const Defer&) = delete;
    Defer(Defer&& other) noexcept
        : m_Fn(std::move(other.m_Fn)),
          m_Active(other.m_Active)
    {
        other.release(); // Prevent the moved-from guard from running
    }
    Defer& operator=(Defer&&) = delete;

    ~Defer() noexcept
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
Defer(F) -> Defer<F>;

#define MLG_DEFER_CAT_1(a, b) a##b
#define MLG_DEFER_CAT(a, b) MLG_DEFER_CAT_1(a, b)

class MLG_DeferHelper
{
public:
    // Tricky operator+ allows us to write "defer + [&](){ ... }".
    // It basically enables the syntax of "defer { ... }".
    template<class F>
    friend auto operator+(MLG_DeferHelper, F&& f)
    {
        return Defer(std::forward<F>(f));
    }
};

// NOLINTNEXTLINE(bugprone-macro-parentheses)
#define MLG_MAKE_DEFERRED MLG_DeferHelper{} + [&]()

#define MLG_DEFER const auto MLG_DEFER_CAT(_defer_, __LINE__) = MLG_MAKE_DEFERRED