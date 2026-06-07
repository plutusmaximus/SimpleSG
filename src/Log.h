#pragma once

#include <cstdint>
#include <format>
#include <memory>
#include <string>

/// Define MLG_LOGGER_NAME before including this header to create a logger with a specific name.
/// Otherwise the default logger is used.
/// Example:
/// #define MLG_LOGGER_NAME "my_logger"
#ifndef MLG_LOGGER_NAME
#define MLG_LOGGER_NAME "****"
#endif

namespace spdlog
{
class logger;
}

class Log final
{
public:

    enum class Level : uint8_t
    {
        Trace = 0,
        Debug,
        Info,
        Warn,
        Error
    };

    class Logger
    {
    public:

        explicit Logger(const std::string& name);

        ~Logger() = default;

        Logger() = delete;
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;

        template<typename... Args>
        void Log(const Level level, std::format_string<Args...> fmt, Args&&... args)
        {
            LogImpl(level, Prefix(std::format(fmt, std::forward<Args>(args)...)));
        }

        void Log(const Level level, const std::string& message)
        {
            LogImpl(level, Prefix(message));
        }

        void SetLevel(const Level level);

    private:

        void LogImpl(const Level level, const std::string& message);

        std::shared_ptr<spdlog::logger> m_Logger;
    };

    /// Log an assertion failure
    template<typename... Args>
    static void Assert(std::format_string<Args...> fmt, Args&&... args)
    {
        s_AssertLogger.Log(Log::Level::Error, std::format(fmt, std::forward<Args>(args)...));
    }

    static void Assert(const std::string& message)
    {
        s_AssertLogger.Log(Log::Level::Error, message);
    }

    /// @brief Sets the global log level.
    static void SetLevel(const Level level);

    template<typename... Args>
    static void PushPrefix(std::format_string<Args...> fmt, Args&&... args)
    {
        PushPrefix(std::format(fmt, std::forward<Args>(args)...));
    }

    static void PushPrefix(const std::string& message);

    static void PopPrefix();

private:

    static inline Logger s_AssertLogger{"ASSERT"};

    static std::string Prefix(const std::string& message);
};

namespace mlg
{
struct LogScope
{
    template<typename... Args>
    LogScope(std::format_string<Args...> fmt, Args&&... args)
    {
        Log::PushPrefix(fmt, std::forward<Args>(args)...);
    }

    LogScope(const std::string& message)
    {
        Log::PushPrefix(message);
    }

    ~LogScope()
    {       
        Log::PopPrefix();
    }

    LogScope() = delete;
    LogScope(const LogScope&) = delete;
    LogScope& operator=(const LogScope&) = delete;
    LogScope(LogScope&&) = delete;
    LogScope& operator=(LogScope&&) = delete;
};
}

// Helper macros for scoping log messages.
// Usage:
//  Log::Debug("Starting...");
//  for(int x = 0; x < 10; ++x)
//  {
//      MLG_LOG_SCOPE("X {}", x);
//      Log::Debug("Next Column...");
//      for(int y = 0; y < 10; ++y)
//      {
//          MLG_LOG_SCOPE("Y {}", y);
//          Log::Debug("Element");
//      }
// }
//
// Example Output:
// [DBG] Starting...
// [DBG] [X 0] Next Column...
// [DBG] [X 0 : Y 0] Element
// [DBG] [X 0 : Y 1] Element
// ...
// [DBG] [X 1] Next Column...
// [DBG] [X 1 : Y 0] Element
// [DBG] [X 1 : Y 1] Element
// ...

#define MLG_LOG_SCOPE_CONCAT_HELPER(a, b) a##b
#define MLG_LOG_SCOPE_CONCAT(a, b) MLG_LOG_SCOPE_CONCAT_HELPER(a, b)
#define MLG_LOG_SCOPE(...) const mlg::LogScope MLG_LOG_SCOPE_CONCAT(logScope_, __LINE__)(__VA_ARGS__);

static inline Log::Logger& MLG_LocalLogger()
{
    static Log::Logger logger(MLG_LOGGER_NAME);
    return logger;
}

#define MLG_TRACE(...) MLG_LocalLogger().Log( Log::Level::Trace, __VA_ARGS__)
#define MLG_DEBUG(...) MLG_LocalLogger().Log( Log::Level::Debug, __VA_ARGS__)
#define MLG_INFO(...) MLG_LocalLogger().Log( Log::Level::Info, __VA_ARGS__)
#define MLG_WARN(...) MLG_LocalLogger().Log( Log::Level::Warn, __VA_ARGS__)
#define MLG_ERROR(...) MLG_LocalLogger().Log( Log::Level::Error, __VA_ARGS__)