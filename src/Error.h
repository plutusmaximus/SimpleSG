#pragma once

#include <expected>
#include <string>
#include <format>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>

/// @brief Logging utility class.
class Logging
{
public:

    static spdlog::logger& GetLogger();

    static spdlog::logger& GetAssertLogger();

    static void SetLogLevel(const spdlog::level::level_enum level);
};

// ====== Logging functions ======

template<typename T>
concept LogFormatString = std::convertible_to<T, std::string> || std::convertible_to<T, std::wstring>;

template<typename... Args>
void logTrace(const LogFormatString auto& format, Args&&... args)
{
    Logging::GetLogger().trace(fmt::runtime(format), std::forward<Args>(args)...);
}

template<typename... Args>
void logDebug(const LogFormatString auto& format, Args&&... args)
{
    Logging::GetLogger().debug(fmt::runtime(format), std::forward<Args>(args)...);
}

template<typename... Args>
void logInfo(const LogFormatString auto& format, Args&&... args)
{
    Logging::GetLogger().info(fmt::runtime(format), std::forward<Args>(args)...);
}

template<typename... Args>
void logError(const LogFormatString auto& format, Args&&... args)
{
    Logging::GetLogger().error(fmt::runtime(format), std::forward<Args>(args)...);
}

/// Log an assertion failure
template<typename... Args>
void logAssert(const LogFormatString auto& format, Args&&... args)
{
    Logging::GetAssertLogger().error(fmt::runtime(format), std::forward<Args>(args)...);
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

    Error(const std::string& message)
        : Error(ErrorCode::System, message)
    {
    }
    
    template<typename... Args>
    Error(std::format_string<Args...> fmt, Args&&... args)
        : Error(ErrorCode::System, fmt, std::forward<Args>(args)...)
    {
    }

    Error(const ErrorCode code, const std::string& message)
        : Code(code)
        , Message(message)
    {
    }
    
    template<typename... Args>
    Error(ErrorCode code, std::format_string<Args...> fmt, Args&&... args)
        : Error(code, std::format(fmt, std::forward<Args>(args)...))
    {
    }

    const ErrorCode Code;

    const std::string Message;
};

namespace std
{
    /// @brief Formatter specialization for Error to support std::format.
    template <>
    struct formatter<Error>
    {
        constexpr auto parse(std::format_parse_context& ctx)
        {
            return ctx.begin(); // No custom parsing; assumes default format
        }

        auto format(const Error& e, std::format_context& ctx) const
        {
            return std::format_to(ctx.out(), "{}", e.Message);
        }
    };
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
    static bool Log(const char* expression, const char* fileName, const int lineNum, bool& mute, const std::string_view format_str = "", Args&&... args)
    {
        std::string message = std::format("{}({}): {}", fileName, lineNum, expression);
        if(!format_str.empty())
        {
            auto userMsg = std::vformat(format_str, std::make_format_args(std::forward<Args>(args)...));
            message = std::format("{} - {}", message, userMsg);
        }

        return Log(message, mute);
    }

    // ===== Expression error helpers ======

    /// @brief Used to create error messages for assertion failures.
    template<typename... Args>
    static std::string MakeExprError(const char* file, const int line, const char* exprStr, std::string_view format, Args&&... args)
    {
        std::string message;
        if(!format.empty())
        {            
            message = std::format(
                "[{}:{}]:({}) {}",
                file,
                line,
                exprStr,
                std::vformat(format, std::make_format_args(args...)));
        }
        else
        {
            message = std::format(
                "[{}:{}]:{}",
                file,
                line,
                exprStr);
        }

        return message;
    }

    static std::string MakeExprError(const char* file, const int line, const char* exprStr, const Error& error)
    {
        return MakeExprError(file, line, exprStr, "{}", error);
    }

    /// @brief Log function for assertion failures that handles variadic arguments.
    template<typename... Args>
    static void LogExprError(const char* file, const int line, const char* exprStr, std::string_view format, Args&&... args)
    {
        std::string message = MakeExprError(file, line, exprStr, format, std::forward<Args>(args)...);

        logError(message);
    }

    static void LogExprError(const char* file, const int line, const char* exprStr, const Error& error)
    {
        std::string message = MakeExprError(file, line, exprStr, error);

        logError(message);
    }
};

/// @brief Representation of a result that can either be a value of type T or an Error.
template<typename T>
using Result = std::expected<T, Error>;

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
#define everify(expr,...) ((static_cast<bool>(expr)) || (Asserts::Log(#expr, __FILE__, __LINE__, Asserts::Muter<__COUNTER__>(), ##__VA_ARGS__) ? __debugbreak(), false : false))

#define eassert(expr,...) void(everify(expr, ##__VA_ARGS__))

#define assert_capture(capName) for(Asserts::Capture capName;!capName.IsCanceled();capName.Cancel())

#else	//NDEBUG

#define	everify(expr) (static_cast<bool>(expr))
#define eassert(expr)

#endif	//NDEBUG

#define LOG_EXPR_ERROR(exprStr, ...) Asserts::LogExprError(__FILE__, __LINE__, exprStr, ##__VA_ARGS__)

#define MAKE_EXPR_ERROR(exprStr, ...) Asserts::MakeExprError(__FILE__, __LINE__, exprStr, ##__VA_ARGS__)

#define etry do

#define ethrow(label) goto label;

#define ecatch(label) while(false);label:

#define ecatchall ecatch(catchall)

#define except(expr, label, ...) while(!static_cast<bool>(expr)){LOG_EXPR_ERROR(#expr, ##__VA_ARGS__);ethrow(label);break;}

#define pcheck(expr, ...) except(expr, catchall, ##__VA_ARGS__)

#define expect(expr, ...) {if(!static_cast<bool>(expr)){return std::unexpected(MAKE_EXPR_ERROR(#expr, ##__VA_ARGS__));}}

//Like expect but also calls verify and pops an assert if false.
#define expectv(expr, ...) {if(!everify(expr)){return std::unexpected(MAKE_EXPR_ERROR(#expr, ##__VA_ARGS__));}}
