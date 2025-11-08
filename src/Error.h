#pragma once

#include <expected>
#include <string>
#include <format>

#include <spdlog/spdlog.h>

class Logging
{
public:

    static spdlog::logger& GetLogger();

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
#if defined(_MSC_VER)

//
// Enable/disable the assert dialog.
// Returns the prior value
//
bool SetAssertDialogEnabled(const bool enabled);

#ifndef NDEBUG

bool ShowAssertDialog(const char* expression, const char* fileName, const int lineNum);

// everify is like assert excpet that it can be used in boolean expressions.
// 
// For ex.
// 
// if(everify(nullptr != p))...
// 
// Or
// 
// return everify(x > y) ? x : -1;
#define everify(expr) ((static_cast<bool>(expr)) || (ShowAssertDialog(#expr, __FILE__, __LINE__) ? __debugbreak(), false : false))

#define eassert(expr) void(everify(expr))

#else	//NDEBUG

#define	everify(expr) (static_cast<bool>(expr))
#define eassert(expr)

#endif	//NDEBUG

#else	//_MSC_VER
#error "Platform not supported"
#endif	//_MSC_VER

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
