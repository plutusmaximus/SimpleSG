#pragma once

#include <string>
#include <format>
#include <string>

#include <spdlog/spdlog.h>

enum class ErrorCode : int
{
    System = 1
};

class Error
{
public:

    Error() = delete;

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

template<typename... Args>
void logDebug(const std::string& format, Args&&... args)
{
    spdlog::debug(fmt::runtime(format), std::forward<Args>(args)...);
}

template<typename... Args>
void logInfo(const std::string& format, Args&&... args)
{
    spdlog::info(fmt::runtime(format), std::forward<Args>(args)...);
}

template<typename... Args>
void logError(const std::string& format, Args&&... args)
{
    spdlog::error(fmt::runtime(format), std::forward<Args>(args)...);
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

#define LOG_EXPR_ERROR(exprStr, ...) LogExprError(__FILE__, __LINE__, exprStr, __VA_ARGS__)

#define MAKE_EXPR_ERROR(exprStr, ...) MakeExprError(__FILE__, __LINE__, exprStr, __VA_ARGS__)

#define etry do

#define everify(expr) {if(!Verify(expr)) ethrow(catchall);}

#define ethrow(label) goto label;

#define ecatch(label) while(false);label:

#define ecatchall ecatch(catchall)

#define except(expr, label, ...) while(!(expr)){LOG_EXPR_ERROR(#expr, __VA_ARGS__);ethrow(label);break;}

#define pcheck(expr, ...) except(expr, catchall, __VA_ARGS__)

#define expect(expr, ...) {if(!(expr)){return std::unexpected(MAKE_EXPR_ERROR(#expr, __VA_ARGS__));}}