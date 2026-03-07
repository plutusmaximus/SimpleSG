#pragma once

#include "Log.h"

#include <format>
#include <string>

class AssertHelper
{
public:
    /// @brief Enable or disable the assert dialog.
    static bool SetDialogEnabled(const bool enabled);

    /// @brief Returns a reference to a boolean that can be used to mute future
    /// assert dialogs at this call site.
    /// Used by the everify macro.
    /// UNIQUE_ID is a unique integer per call site.
    template<int UNIQUE_ID>
    static inline bool& Muter()
    {
        static bool muted = false;
        return muted;
    }

    /// @brief Log an assertion failure.
    template<typename... Args>
    static bool Log(const char* expression,
        const char* fileName,
        const int lineNum,
        bool& mute,
        std::format_string<Args...> fmt,
        Args&&... args)
    {
        std::string userMsg = std::format(fmt, std::forward<Args>(args)...);
        std::string message = std::format("{}({}): {} - {}", fileName, lineNum, expression, userMsg);

        return Log(message, mute);
    }

    /// @brief Log an assertion failure.
    static bool Log(const char* expression,
        const char* fileName,
        const int lineNum,
        bool& mute)
    {
        std::string message = std::format("{}({}): {}", fileName, lineNum, expression);

        return Log(message, mute);
    }

private:

    /// @brief Log an assertion failure.
    static bool Log(const std::string& message, bool& mute);
};

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
        (AssertHelper::Log(#expr, __FILE__, __LINE__, AssertHelper::Muter<__COUNTER__>(), ##__VA_ARGS__) \
            ? (__debugbreak(), false) : false))

#define MLG_ASSERT(expr, ...) void(MLG_VERIFY(expr, ##__VA_ARGS__))

#define MLG_ASSERT_CAPTURE(capName) \
    for(struct{bool en = AssertHelper::SetDialogEnabled(false); \
        Log::Capture capName; \
        std::string Message(){return capName.Message();}} capName; \
        !capName.capName.IsCanceled(); \
        capName.capName.Cancel(), AssertHelper::SetDialogEnabled(capName.en))

#else // NDEBUG

#define MLG_VERIFY(expr, ...) (static_cast<bool>(expr))
#define MLG_ASSERT(expr, ...)

#define MLG_ASSERT_CAPTURE(capName)

#endif // NDEBUG
