#pragma once

#include "imstring.h"
#include "Logging.h"

#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

/// @brief Assertion helper class.
class Asserts
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

    /// @brief Log an assertion failure and display a dialog if enabled.
    static bool Log(const std::string_view message, bool& mute);

    /// @brief Log an assertion failure and display a dialog if enabled.
    template<typename... Args>
    static bool Log(const char* expression,
        const char* fileName,
        const int lineNum,
        bool& mute,
        const std::string_view format_str = "",
        Args&&... args)
    {
        std::string message = std::format("{}({}): {}", fileName, lineNum, expression);
        if(!format_str.empty())
        {
            auto userMsg =
                std::vformat(format_str, std::make_format_args(std::forward<Args>(args)...));
            message = std::format("{} - {}", message, userMsg);
        }

        return Log(message, mute);
    }
};

#ifndef NDEBUG

// everify is like assert excpet that it can be used in boolean expressions.
//
// For ex.
//
// if(everify(nullptr != p))...
//
// Or
//
// return everify(x > y) ? x : -1;
#define everify(expr, ...)                                                                         \
    ((static_cast<bool>(expr)) ||                                                                  \
        (Asserts::Log(#expr, __FILE__, __LINE__, Asserts::Muter<__COUNTER__>(), ##__VA_ARGS__)     \
            ? __debugbreak(),                                                                      \
            false                                                                                  \
            : false))

#define eassert(expr, ...) void(everify(expr, ##__VA_ARGS__))

#define assert_capture(capName)                                                                    \
    for(struct{bool en = Asserts::SetDialogEnabled(false);\
        Log::Capture capName;\
        std::string Message(){return capName.Message();}} capName;\
        !capName.capName.IsCanceled();\
        capName.capName.Cancel(), Asserts::SetDialogEnabled(capName.en))

#else // NDEBUG

#define everify(expr, ...) (static_cast<bool>(expr))
#define eassert(expr, ...)

#define assert_capture(capName)

#endif // NDEBUG
