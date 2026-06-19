#pragma once

#include "Log.h"

#include <SDL3/SDL_assert.h>

namespace AssertHelper
{
struct AssertData
{
    SDL_AssertData sdlAssertData{};
    SDL_AssertState sdlAssertState{SDL_ASSERTION_BREAK};
};

// This function must have internal linkage to avoid
// conflicts between different translation units.
// IOW it must be declared static at file scope.
// If not static then different TUs could have access
// to the same assert data.
//
// DO NOT REMOVE THE static decl!!!
template<int UNIQUE_ID, size_t N>
static AssertHelper::AssertData& GetAssertData(const char(&expression)[N])
{
    // Store a copy of the expression in a static buffer
    // to guarantee it's lifetime.
    static char expr[N];
    [[maybe_unused]] static const bool copied = std::copy_n(&expression[0], N, &expr[0]) == (&expr[N]);
    static AssertHelper::AssertData assertData{ {false, 0, &expr[0], nullptr, 0, nullptr, nullptr}, SDL_ASSERTION_BREAK };
    return assertData;
}

/// @brief Log an assertion failure.
bool Log(AssertHelper::AssertData& assertData,
    const char* expression,
    const char* function,
    const char* fileName,
    const int lineNum,
    const std::string_view& userMsg);

/// @brief Log an assertion failure.
bool Log(AssertHelper::AssertData& assertData,
    const char* expression,
    const char* function,
    const char* fileName,
    const int lineNum);

/// @brief Log an assertion failure.
template<typename... Args>
static bool Log(AssertHelper::AssertData& assertData,
    const char* expression,
    const char* function,
    const char* fileName,
    const int lineNum,
    std::format_string<Args...> fmt,
    Args&&... args)
{
    const std::string userMsg = std::format(fmt, std::forward<Args>(args)...);

    return AssertHelper::Log(assertData, expression, function, fileName, lineNum, userMsg);
}

}   // namespace AssertHelper

// Disable the warning about __COUNTER__ being a C2y extension.
#if defined(__clang__)
#  define MLG_CLANG_DIAG_PUSH _Pragma("clang diagnostic push")
#  define MLG_CLANG_DIAG_POP  _Pragma("clang diagnostic pop")
#  define MLG_CLANG_DIAG_IGNORE_C2Y_EXTENSIONS \
      _Pragma("clang diagnostic ignored \"-Wc2y-extensions\"")
#else
#  define MLG_CLANG_DIAG_PUSH
#  define MLG_CLANG_DIAG_POP
#  define MLG_CLANG_DIAG_IGNORE_C2Y_EXTENSIONS
#endif

#ifndef NDEBUG

// MLG_VERIFY is like MLG_ASSERT excpet that it can be used in boolean expressions.
//
// For ex.
//
// if(MLG_VERIFY(nullptr != p))...
//
// Or
//
// return MLG_VERIFY(x > y) ? x : -1;

#define MLG_VERIFY(expr, ...) \
    MLG_CLANG_DIAG_PUSH \
    MLG_CLANG_DIAG_IGNORE_C2Y_EXTENSIONS \
    (static_cast<bool>(expr) || \
        (AssertHelper::Log(AssertHelper::GetAssertData<__COUNTER__>(#expr),#expr, __func__, __FILE__, __LINE__ __VA_OPT__(, ) __VA_ARGS__) \
            ? (SDL_AssertBreakpoint(), false) : false)) \
    MLG_CLANG_DIAG_POP

#define MLG_ASSERT(expr, ...) void(MLG_VERIFY(expr __VA_OPT__(,) __VA_ARGS__))

// MLG_ASSERT_ONLY is for expressions that are only used in asserts, but we want to avoid "unused
// variable/expression" warnings in release builds.
#define MLG_ASSERT_ONLY(expr) (void)(expr)

#else // NDEBUG

#define MLG_VERIFY(expr, ...) (static_cast<bool>(expr))
#define MLG_ASSERT(expr, ...)

#define MLG_ASSERT_ONLY(expr)

#endif // NDEBUG

// Like MLG_CHECK except if the condition is false the program will abort.
// Used to check for invariants that if violated result in undefined behavior.
// I.e. things that have no chance of recovery, and which cannot return an error
// to callers, e.g. constructors.
#define MLG_REQUIRE(expr, ...) \
    MLG_CLANG_DIAG_PUSH \
    MLG_CLANG_DIAG_IGNORE_C2Y_EXTENSIONS \
    while(!static_cast<bool>(expr)) \
    { \
        AssertHelper::Log(AssertHelper::GetAssertData<__COUNTER__>(#expr),#expr, __func__, __FILE__, __LINE__ __VA_OPT__(, ) __VA_ARGS__); \
        std::abort(); \
    } \
    MLG_CLANG_DIAG_POP