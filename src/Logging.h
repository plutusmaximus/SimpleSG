#pragma once

#include "imstring.h"

#include <concepts>
#include <cstddef>
#include <format>
#include <memory>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>

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

template<typename... Args>
inline void
logTrace(std::format_string<Args...> fmt, Args&&... args)
{
    GetLogger<__LOGGER_NAME__>()->trace(std::format(fmt, std::forward<Args>(args)...));
}

inline void
logTrace(std::string_view message)
{
    GetLogger<__LOGGER_NAME__>()->trace(message);
}

template<typename... Args>
inline void
logDebug(std::format_string<Args...> fmt, Args&&... args)
{
    GetLogger<__LOGGER_NAME__>()->debug(std::format(fmt, std::forward<Args>(args)...));
}

inline void
logDebug(std::string_view message)
{
    GetLogger<__LOGGER_NAME__>()->debug(message);
}

template<typename... Args>
inline void
logInfo(std::format_string<Args...> fmt, Args&&... args)
{
    GetLogger<__LOGGER_NAME__>()->info(std::format(fmt, std::forward<Args>(args)...));
}

inline void
logInfo(std::string_view message)
{
    GetLogger<__LOGGER_NAME__>()->info(message);
}

template<typename... Args>
inline void
logWarn(std::format_string<Args...> fmt, Args&&... args)
{
    GetLogger<__LOGGER_NAME__>()->warn(std::format(fmt, std::forward<Args>(args)...));
}

inline void
logWarn(std::string_view message)
{
    GetLogger<__LOGGER_NAME__>()->warn(message);
}

template<typename... Args>
inline void
logError(std::format_string<Args...> fmt, Args&&... args)
{
    GetLogger<__LOGGER_NAME__>()->error(std::format(fmt, std::forward<Args>(args)...));
}

inline void
logError(std::string_view message)
{
    GetLogger<__LOGGER_NAME__>()->error(message);
}

/// Log an assertion failure
template<typename... Args>
inline void
logAssert(std::format_string<Args...> fmt, Args&&... args)
{
    GetLogger<"assert">()->error(std::format(fmt, std::forward<Args>(args)...));
}

inline void
logAssert(std::string_view message)
{
    GetLogger<"assert">()->error(message);
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
