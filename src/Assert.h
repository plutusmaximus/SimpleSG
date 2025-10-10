#pragma once

#include <string>
#include <format>
#include <spdlog/spdlog.h>

#if defined(_MSC_VER)

//
// Enable/disable the assert dialog.
// Returns the prior value
//
bool SetAssertDialogEnabled(const bool enabled);

#ifndef NDEBUG

bool ShowAssertDialog(const char* expression, const char* fileName, const int lineNum);

#define Assert(expression)(void) ((!!(expression)) || (ShowAssertDialog(#expression, __FILE__, __LINE__) ? __debugbreak(), false : false))

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

#define LOG_EXPR_ERROR(exprStr, ...) LogExprError(__FILE__, __LINE__, exprStr, __VA_ARGS__)

#define MAKE_EXPR_ERROR(exprStr, ...) MakeExprError(__FILE__, __LINE__, exprStr, __VA_ARGS__)

// verify is like assert excpet that it can be used in boolean expressions.
// 
// For ex.
// 
// if(Verify(nullptr != p))...
// 
// Or
// 
// return Verify(x > y) ? x : -1;
// 
// !! is used to ensure that any overloaded operators used to evaluate expr
// do not end up at &&.
#define Verify(expression) ((!!(expression)) || (ShowAssertDialog(#expression, __FILE__, __LINE__) ? __debugbreak(), false : false))

#else	//NDEBUG

#define	Verify(expr) (!!(expr))

#endif	//NDEBUG

#else	//_MSC_VER
#error "Platform not supported"
#endif	//_MSC_VER

#define ptry do

#define pverify(expr) {if(!Verify(expr)) pthrow(catchall);}

#define pthrow(label) goto label;

#define pcatch(label) while(false);label:

#define pcatchall pcatch(catchall)

#define except(expr, label, ...) while(!(expr)){LOG_EXPR_ERROR(#expr, __VA_ARGS__);pthrow(label);break;}

#define pcheck(expr, ...) except(expr, catchall, __VA_ARGS__)

#define expect(expr, ...) {if(!(expr)){return std::unexpected(MAKE_EXPR_ERROR(#expr, __VA_ARGS__));}}