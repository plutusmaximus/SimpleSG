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
