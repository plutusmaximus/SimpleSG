#pragma once

#include <expected>
#include <string>
#include <format>
#include <memory>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/sink.h>

class Logging
{
public:

    static spdlog::logger& GetLogger();

    static spdlog::logger& GetAssertLogger();

    static void SetLogLevel(const spdlog::level::level_enum level);
};

enum class ErrorCode : int
{
    System = 1
};

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

/// @brief Assertion helper class.
class Asserts
{
public:

    /// @brief Enable or disable the assert dialog.
    static bool SetDialogEnabled(const bool enabled);

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

    /// @brief Log an assert and show the assert dialog (if dialogs are enabled).
    /// Returns true to break into the debugger, false to continue execution.
    static bool Log(const char* expression, const char* fileName, const int lineNum, bool& mute);
};

template<typename T>
using Result = std::expected<T, Error>;

template<typename... Args>
void logTrace(const std::string& format, Args&&... args)
{
    Logging::GetLogger().trace(fmt::runtime(format), std::forward<Args>(args)...);
}

template<typename... Args>
void logTrace(const std::wstring& format, Args&&... args)
{
    Logging::GetLogger().trace(fmt::runtime(format), std::forward<Args>(args)...);
}

template<typename... Args>
void logDebug(const std::string& format, Args&&... args)
{
    Logging::GetLogger().debug(fmt::runtime(format), std::forward<Args>(args)...);
}

template<typename... Args>
void logDebug(const std::wstring& format, Args&&... args)
{
    Logging::GetLogger().debug(fmt::runtime(format), std::forward<Args>(args)...);
}

template<typename... Args>
void logInfo(const std::string& format, Args&&... args)
{
    Logging::GetLogger().info(fmt::runtime(format), std::forward<Args>(args)...);
}

template<typename... Args>
void logInfo(const std::wstring& format, Args&&... args)
{
    Logging::GetLogger().info(fmt::runtime(format), std::forward<Args>(args)...);
}

template<typename... Args>
void logError(const std::string& format, Args&&... args)
{
    Logging::GetLogger().error(fmt::runtime(format), std::forward<Args>(args)...);
}

template<typename... Args>
void logError(const std::wstring& format, Args&&... args)
{
    Logging::GetLogger().error(fmt::runtime(format), std::forward<Args>(args)...);
}

/// Log an assertion failure
template<typename... Args>
void logAssert(const std::string& format, Args&&... args)
{
    Logging::GetAssertLogger().error(fmt::runtime(format), std::forward<Args>(args)...);
}

/// Log an assertion failure
template<typename... Args>
void logAssert(const std::wstring& format, Args&&... args)
{
    Logging::GetAssertLogger().error(fmt::runtime(format), std::forward<Args>(args)...);
}

template<typename... Args>
inline std::string MakeExprError(const char* file, const int line, const char* exprStr, std::string_view format, Args&&... args)
{
    std::string message = std::format(
        "[{}:{}]:({}) {}",
        file,
        line,
        exprStr,
        std::vformat(format, std::make_format_args(args...)));

    return message;
}

inline std::string MakeExprError(const char* file, const int line, const char* exprStr, const Error& error)
{
    std::string message = std::format(
        "[{}:{}]:({}) {}",
        file,
        line,
        exprStr,
        error.Message);

    return message;
}

// Log function that handles variadic arguments
template<typename... Args>
inline void LogExprError(const char* file, const int line, const char* exprStr, std::string_view format, Args&&... args)
{
    std::string message = MakeExprError(file, line, exprStr, format, std::forward<Args>(args)...);

    logError(message);
}

inline void LogExprError(const char* file, const int line, const char* exprStr)
{
    std::string message = std::format(
        "[{}:{}]:{}",
        file,
        line,
        exprStr);

    logError(message);
}

inline void LogExprError(const char* file, const int line, const char* exprStr, const Error& error)
{
    std::string message = MakeExprError(file, line, exprStr, error);

    logError(message);
}

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
#define everify(expr) ((static_cast<bool>(expr)) || (Asserts::Log(#expr, __FILE__, __LINE__, Asserts::Muter<__COUNTER__>()) ? __debugbreak(), false : false))

#define eassert(expr) void(everify(expr))

#define assert_capture(capName) for(Asserts::Capture capName;!capName.IsCanceled();capName.Cancel())

#else	//NDEBUG

#define	everify(expr) (static_cast<bool>(expr))
#define eassert(expr)

#endif	//NDEBUG

#define LOG_EXPR_ERROR(exprStr, ...) LogExprError(__FILE__, __LINE__, exprStr, __VA_ARGS__)

#define MAKE_EXPR_ERROR(exprStr, ...) MakeExprError(__FILE__, __LINE__, exprStr, __VA_ARGS__)

#define etry do

#define ethrow(label) goto label;

#define ecatch(label) while(false);label:

#define ecatchall ecatch(catchall)

#define except(expr, label, ...) while(!static_cast<bool>(expr)){LOG_EXPR_ERROR(#expr, __VA_ARGS__);ethrow(label);break;}

#define pcheck(expr, ...) except(expr, catchall, __VA_ARGS__)

#define expect(expr, ...) {if(!static_cast<bool>(expr)){return std::unexpected(MAKE_EXPR_ERROR(#expr, __VA_ARGS__));}}

//Like expect but also calls verify and pops an assert if false.
#define expectv(expr, ...) {if(!everify(expr)){return std::unexpected(MAKE_EXPR_ERROR(#expr, __VA_ARGS__));}}
