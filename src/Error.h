#pragma once

#include "imstring.h"

#include <format>
#include <spdlog/sinks/sink.h>
#include <spdlog/spdlog.h>
#include <string>
#include <variant>

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
inline std::shared_ptr<spdlog::logger> __LOGGER__ = LogHelper::CreateLogger(S.sv());

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
    __LOGGER__<__LOGGER_NAME__>->trace(std::vformat(format, std::make_format_args(args...)));
}

template<typename... Args>
inline void
logDebug(const LogFormatString auto& format, Args&&... args)
{
    __LOGGER__<__LOGGER_NAME__>->debug(std::vformat(format, std::make_format_args(args...)));
}

template<typename... Args>
inline void
logInfo(const LogFormatString auto& format, Args&&... args)
{
    __LOGGER__<__LOGGER_NAME__>->info(std::vformat(format, std::make_format_args(args...)));
}

template<typename... Args>
inline void
logWarn(const LogFormatString auto& format, Args&&... args)
{
    __LOGGER__<__LOGGER_NAME__>->warn(std::vformat(format, std::make_format_args(args...)));
}

template<typename... Args>
inline void
logError(const LogFormatString auto& format, Args&&... args)
{
    __LOGGER__<__LOGGER_NAME__>->error(std::vformat(format, std::make_format_args(args...)));
}

/// Log an assertion failure
template<typename... Args>
inline void
logAssert(const LogFormatString auto& format, Args&&... args)
{
    __LOGGER__<"assert">->error(std::vformat(format, std::make_format_args(args...)));
}

/// @brief Sets the log level for a specific logger.
template<LoggerLabel S>
inline void
logSetLevel(const spdlog::level::level_enum level)
{
    __LOGGER__<S>->SetLevel(level);
}

/// @brief Sets the global log level.
inline void
logSetLevel(const spdlog::level::level_enum level)
{
    spdlog::set_level(level);
}

/// @brief Error code enumeration.
enum class ErrorCode : int
{
    System = 1
};

/// @brief Representation of an error with code and message.
class Error
{
public:
    Error() = delete;

    Error(const char* message)
        : Error(ErrorCode::System, message)
    {
    }

    Error(std::string_view message)
        : Error(ErrorCode::System, message)
    {
    }

    template<typename... Args>
    Error(std::format_string<Args...> fmt, Args&&... args)
        : Error(ErrorCode::System, fmt, std::forward<Args>(args)...)
    {
    }

    Error(const ErrorCode code, std::string_view message)
        : Code(code),
          Message(message)
    {
    }

    template<typename... Args>
    Error(ErrorCode code, std::format_string<Args...> fmt, Args&&... args)
        : Error(code, std::format(fmt, std::forward<Args>(args)...))
    {
    }

    bool operator==(const Error& other) const
    {
        return Code == other.Code && Message == other.Message;
    }

    bool operator!=(const Error& other) const { return !(*this == other); }

    const ErrorCode Code;

    const imstring Message;
};

/// @brief Formatter specialization for Error to support std::format.
template<>
struct std::formatter<Error> : std::formatter<imstring>
{
    auto format(const Error& e, std::format_context& ctx) const
    {
        return std::formatter<imstring>::format(e.Message, ctx);
    }
};

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

    // ===== Expression error helpers ======

    /// @brief Used to create error messages for assertion failures.
    template<typename... Args>
    static Error MakeExprError(const char* file,
        const int line,
        const char* exprStr,
        std::string_view format,
        Args&&... args)
    {
        std::string message;
        if(!format.empty())
        {
            message = std::format("[{}:{}]:({}) {}",
                file,
                line,
                exprStr,
                std::vformat(format, std::make_format_args(args...)));
        }
        else
        {
            message = std::format("[{}:{}]:{}", file, line, exprStr);
        }

        return Error(message);
    }

    static Error MakeExprError(
        const char* file, const int line, const char* exprStr, const Error& error)
    {
        return MakeExprError(file, line, exprStr, "{}", error);
    }
};

/// @brief Representation of a result that can either be a value of type T or an Error.
template<typename T>
class Result
{
public:
    Result() = default;

    Result(const T& t)
        : m_ValueOrError(t)
    {
    }

    Result(T&& t)
        : m_ValueOrError(std::move(t))
    {
    }

    Result(const Error& error)
        : m_ValueOrError(error)
    {
    }

    Result(Error&& error)
        : m_ValueOrError(std::move(error))
    {
    }

    Result(const Result& other) = default;
    Result(Result&& other) = default;
    Result& operator=(const Result& other) = default;
    Result& operator=(Result&& other) = default;

    constexpr T& value() &
    {
        return std::get<T>(m_ValueOrError);
    }
    constexpr const T& value() const&
    {
        return std::get<T>(m_ValueOrError);
    }
    constexpr T&& value() &&
    {
        return std::move(std::get<T>(m_ValueOrError));
    }
    constexpr const T&& value() const&&
    {
        return std::move(std::get<T>(m_ValueOrError));
    }

    constexpr T& operator*() & { return value(); }
    constexpr const T& operator*() const& { return value(); }
    constexpr T&& operator*() && { return std::move(value()); }
    constexpr const T&& operator*() const&& { return std::move(value()); }

    constexpr T* operator->() { return &value(); }
    constexpr const T* operator->() const { return &value(); }

    constexpr Error& error() & { return std::get<Error>(m_ValueOrError); }
    constexpr const Error& error() const& { return std::get<Error>(m_ValueOrError); }
    constexpr Error&& error() && { return std::move(std::get<Error>(m_ValueOrError)); }
    constexpr const Error&& error() const&& { return std::move(std::get<Error>(m_ValueOrError)); }

    bool has_value() const { return std::holds_alternative<T>(m_ValueOrError); }

    bool has_error() const { return std::holds_alternative<Error>(m_ValueOrError); }

    operator bool() const { return has_value(); }

    bool operator==(const Result& other) const
    {
        if(has_value() != other.has_value())
        {
            return false;
        }

        if(has_value())
        {
            return value() == other.value();
        }
        else
        {
            return error() == other.error();
        }
    }

    bool operator!=(const Result& other) const { return !(*this == other); }

private:
    std::variant<T, Error> m_ValueOrError;
};

/// @brief Specialization of Result for void type.
template<>
class Result<void>
{
public:
    Result() = default;

    Result(const Error& error)
        : m_ValueOrError(error)
    {
    }

    Result(Error&& error)
        : m_ValueOrError(std::move(error))
    {
    }

    Result(const Result& other) = default;
    Result(Result&& other) = default;
    Result& operator=(const Result& other) = default;
    Result& operator=(Result&& other) = default;

    constexpr Error& error() & { return std::get<Error>(m_ValueOrError); }
    constexpr const Error& error() const& { return std::get<Error>(m_ValueOrError); }
    constexpr Error&& error() && { return std::move(std::get<Error>(m_ValueOrError)); }
    constexpr const Error&& error() const&& { return std::move(std::get<Error>(m_ValueOrError)); }

    bool has_error() const { return std::holds_alternative<Error>(m_ValueOrError); }

    operator bool() const { return !has_error(); }

    bool operator==(const Result& other) const
    {
        if(has_error() != other.has_error())
        {
            return false;
        }

        if(has_error())
        {
            return (error() == other.error());
        }

        return true;
    }

    bool operator!=(const Result& other) const { return !(*this == other); }

private:
    std::variant<std::monostate, Error> m_ValueOrError;
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

#define MAKE_EXPR_ERROR(exprStr, ...)                                                              \
    Asserts::MakeExprError(__FILE__, __LINE__, exprStr, ##__VA_ARGS__)

#define expect(expr, ...)                                                                          \
    {                                                                                              \
        if(!static_cast<bool>(expr))                                                               \
        {                                                                                          \
            return Error(MAKE_EXPR_ERROR(#expr, ##__VA_ARGS__));                                   \
        }                                                                                          \
    }

// Like expect but also calls verify and pops an assert if false.
#define expectv(expr, ...)                                                                         \
    {                                                                                              \
        if(!everify(expr))                                                                         \
        {                                                                                          \
            return Error(MAKE_EXPR_ERROR(#expr, ##__VA_ARGS__));                                   \
        }                                                                                          \
    }
