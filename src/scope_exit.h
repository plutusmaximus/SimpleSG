#pragma once
#include <version>

#if defined(__cpp_lib_scope_exit) && __cpp_lib_scope_exit >= 202011L
#include <scope>

template<class F>
using scope_exit = std::scope_exit<F>;

#else
#include <utility>
#include <type_traits>

/// @brief A scope guard that executes a provided callable when it goes out of scope.
/// This is a replacement for std::scope_exit in case it's not available.
/// As of MSVC 2022, std::scope_exit is not available.
template<class F>
class scope_exit
{
public:
    explicit scope_exit(F&& f) noexcept(std::is_nothrow_move_constructible_v<F>)
        : f_(std::forward<F>(f)), active_(true) {}

    scope_exit(scope_exit&& other) noexcept(std::is_nothrow_move_constructible_v<F>)
        : f_(std::move(other.f_)), active_(other.active_)
    {
        other.active_ = false;
    }

    scope_exit(const scope_exit&) = delete;
    scope_exit& operator=(const scope_exit&) = delete;
    scope_exit& operator=(scope_exit&&) = delete;

    ~scope_exit() noexcept(noexcept(std::declval<F&>()()))
    {
        if (active_) f_();
    }

    void release() noexcept { active_ = false; }

private:
    F f_;
    bool active_;
};

template<class F>
scope_exit(F) -> scope_exit<F>;

#endif