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
template<int UNIQUE_ID>
static inline AssertHelper::AssertData& GetAssertData()
{
    static AssertHelper::AssertData assertData{};
    return assertData;
}

/// @brief Log an assertion failure.
bool Log(AssertHelper::AssertData& assertData,
    const char* expression,
    const char* function,
    const char* fileName,
    const int lineNum,
    const std::string& userMsg);

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
    (static_cast<bool>(expr) || \
        (AssertHelper::Log(AssertHelper::GetAssertData<__COUNTER__>(),#expr, SDL_FUNCTION, SDL_ASSERT_FILE, SDL_LINE __VA_OPT__(, ) __VA_ARGS__) \
            ? (SDL_AssertBreakpoint(), false) : false))

#define MLG_ASSERT(expr, ...) void(MLG_VERIFY(expr __VA_OPT__(,) __VA_ARGS__))

#define MLG_ASSERT2(expr, ...) void(MLG_VERIFY2(expr __VA_OPT__(,) __VA_ARGS__))

// MLG_ASSERT_ONLY is for expressions that are only used in asserts, but we want to avoid "unused
// variable/expression" warnings in release builds.
#define MLG_ASSERT_ONLY(expr) (void)(expr)

#else // NDEBUG

#define MLG_VERIFY(expr, ...) (static_cast<bool>(expr))
#define MLG_ASSERT(expr, ...)

#define MLG_ASSERT_ONLY(expr)

#endif // NDEBUG
