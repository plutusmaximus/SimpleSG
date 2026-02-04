#pragma once

#include "imstring.h"

#include <concepts>
#include <cstddef>
#include <format>
#include <memory>
#include <spdlog/sinks/sink.h>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>

// ====== Logger instance ======

/// @brief  consteval string label that can be used as a non-type template parameter
/// to specialize loggers by label.
template<std::size_t N>
struct LoggerLabel
{
    char value[N];

    consteval LoggerLabel(const char (&str)[N])
    {
        for(std::size_t i = 0; i < N; ++i)
        {
            value[i] = str[i];
        }
    }

    consteval std::string_view sv() const { return std::string_view(value, N - 1); }
};

class LogHelper
{
public:
    static std::shared_ptr<spdlog::logger> CreateLogger(const std::string_view name);
};

/// Define __LOGGER_NAME__ before including this header to create a logger with a specific name.
/// Otherwise the default logger is used.
/// Example:
/// #define __LOGGER_NAME__ "my_logger"
#ifndef __LOGGER_NAME__
#define __LOGGER_NAME__ "****"
#endif

/// @brief Global instance of a logger specialized by label.
template<LoggerLabel S>
inline std::shared_ptr<spdlog::logger> GetLogger()
{
    static std::shared_ptr<spdlog::logger> logger = LogHelper::CreateLogger(S.sv());

    return logger;
}

// ====== Logging functions ======

/// @brief Concept to constrain format string types.
template<typename T>
concept LogFormatString =
    std::convertible_to<T, const char*> || std::convertible_to<T, std::string> ||
    std::convertible_to<T, std::wstring> || std::convertible_to<T, imstring>;

template<typename... Args>
inline void
logTrace(const LogFormatString auto& format, Args&&... args)
{
    GetLogger<__LOGGER_NAME__>()->trace(std::vformat(format, std::make_format_args(args...)));
}

template<typename... Args>
inline void
logDebug(const LogFormatString auto& format, Args&&... args)
{
    GetLogger<__LOGGER_NAME__>()->debug(std::vformat(format, std::make_format_args(args...)));
}

template<typename... Args>
inline void
logInfo(const LogFormatString auto& format, Args&&... args)
{
    GetLogger<__LOGGER_NAME__>()->info(std::vformat(format, std::make_format_args(args...)));
}

template<typename... Args>
inline void
logWarn(const LogFormatString auto& format, Args&&... args)
{
    GetLogger<__LOGGER_NAME__>()->warn(std::vformat(format, std::make_format_args(args...)));
}

template<typename... Args>
inline void
logError(const LogFormatString auto& format, Args&&... args)
{
    GetLogger<__LOGGER_NAME__>()->error(std::vformat(format, std::make_format_args(args...)));
}

/// Log an assertion failure
template<typename... Args>
inline void
logAssert(const LogFormatString auto& format, Args&&... args)
{
    GetLogger<"assert">()->error(std::vformat(format, std::make_format_args(args...)));
}

/// @brief Sets the log level for a specific logger.
template<LoggerLabel S>
inline void
logSetLevel(const spdlog::level::level_enum level)
{
    GetLogger<S>()->SetLevel(level);
}

/// @brief Sets the global log level.
inline void
logSetLevel(const spdlog::level::level_enum level)
{
    spdlog::set_level(level);
}

/// @brief Assertion helper class.
class Asserts
{
public:
    /// @brief Enable or disable the assert dialog.
    static bool SetDialogEnabled(const bool enabled);

    /// @brief RAII class to capture assert messages.
    /// Assert dialogs are disabled while this object is alive.
    /// Usage:
    /// {
    ///     Asserts::Capture capture;
    ///     // code that may trigger asserts
    /// }
    /// If you want to cancel the capture before destruction, call Cancel().
    /// Otherwise, the capture will be canceled automatically in the destructor.
    /// Typically the eassert_capture macro is used to simplify usage.
    /// Example:
    ///     assert_capture(capture)
    ///     {
    ///         // code that may trigger asserts
    ///         // use capture.Message() to get the last assert message
    ///         EXPECT_TRUE(capture.Message().contains("expected text"));
    ///     }
    class Capture
    {
    public:
        Capture();

        void Cancel();

        bool IsCanceled() const;

        std::string Message() const;

        ~Capture();

    private:
        const bool m_OldValue;

        bool m_Canceled = false;

        std::shared_ptr<spdlog::sinks::sink> m_Sink;
    };

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
    for(Asserts::Capture capName; !capName.IsCanceled(); capName.Cancel())

#else // NDEBUG

#define everify(expr) (static_cast<bool>(expr))
#define eassert(expr)

#endif // NDEBUG
