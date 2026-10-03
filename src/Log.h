#pragma once

#include <cstdint>
#include <memory>
#include <source_location>
#include <spdlog/fmt/fmt.h>
#include <string_view>

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
    /// Maximum size of the buffer used for formatting log messages.
    static constexpr size_t kMaxSizeofFormatBuffer = 512;

public:
    enum class Level : uint8_t
    {
        Trace = 0,
        Debug,
        Info,
        Warn,
        Error
    };

    /// Formats a message into the provided buffer and returns a string view of the formatted message.
    /// If the formatted message exceeds the buffer size, it will be truncated and an ellipsis will be appended.
    template<size_t N, typename... Args>
    static std::string_view FormatToBuffer(
        char (&buffer)[N], fmt::format_string<Args...> fmtStr, Args&&... args)
    {
        static_assert(N >= 3, "Buffer size must be at least 3 to accommodate ellipsis.");

        auto result = fmt::format_to_n(&buffer[0],
            N,
            fmtStr,
            std::forward<Args>(args)...);

        size_t formattedSize = result.size;
        if(formattedSize > N)
        {
            // Append ellipsis to indicate truncation.
            buffer[N - 3] = '.';
            buffer[N - 2] = '.';
            buffer[N - 1] = '.';
            formattedSize = N;
        }

        return std::string_view(&buffer[0], formattedSize);
    }

    class Logger
    {
    public:
        explicit Logger(std::string_view name);

        ~Logger() = default;

        Logger() = delete;
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;

        void Log(const Level level,
            const std::source_location& srcLoc,
            const std::string_view message);

        template<typename... Args>
        void Log(const Level level,
            const std::source_location& srcLoc,
            fmt::format_string<Args...> fmtStr,
            Args&&... args)
        {
            char buffer[kMaxSizeofFormatBuffer];
            const std::string_view formattedMsg =
                FormatToBuffer(buffer, fmtStr, std::forward<Args>(args)...);
            Log(level, srcLoc, formattedMsg);
        }

        void SetLevel(const Level level);

    private:
        void LogImpl(const Level level, const std::string_view message);

        std::shared_ptr<spdlog::logger> m_Logger;
    };

    static void LogAssert(const std::source_location& srcLoc, const std::string_view message);

    /// Sets the global log level.
    static void SetLevel(const Level level);

    template<typename... Args>
    static void PushPrefix(fmt::format_string<Args...> fmtStr, Args&&... args)
    {
        char buffer[kMaxSizeofFormatBuffer];
        const std::string_view formattedPrefix =
            FormatToBuffer(buffer, fmtStr, std::forward<Args>(args)...);
        PushPrefix(formattedPrefix);
    }

    static void PushPrefix(const std::string_view message);

    static void PopPrefix();
};

namespace mlg
{
struct LogScope
{
    template<typename... Args>
    explicit LogScope(fmt::format_string<Args...> fmtStr, Args&&... args)
    {
        Log::PushPrefix(fmtStr, std::forward<Args>(args)...);
    }

    explicit LogScope(std::string_view message) { Log::PushPrefix(message); }

    ~LogScope() { Log::PopPrefix(); }

    LogScope() = delete;
    LogScope(const LogScope&) = delete;
    LogScope& operator=(const LogScope&) = delete;
    LogScope(LogScope&&) = delete;
    LogScope& operator=(LogScope&&) = delete;
};
} // namespace mlg

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
#define MLG_LOG_SCOPE(...)                                                                         \
    const mlg::LogScope MLG_LOG_SCOPE_CONCAT(logScope_, __LINE__)(__VA_ARGS__);

static inline Log::Logger&
MLG_LocalLogger()
{
    static Log::Logger logger(MLG_LOGGER_NAME);
    return logger;
}

#define MLG_TRACE(...) MLG_LocalLogger().Log(Log::Level::Trace, std::source_location::current()__VA_OPT__(, ) __VA_ARGS__)
#define MLG_DEBUG(...) MLG_LocalLogger().Log(Log::Level::Debug, std::source_location::current()__VA_OPT__(, ) __VA_ARGS__)
#define MLG_INFO(...) MLG_LocalLogger().Log(Log::Level::Info, std::source_location::current()__VA_OPT__(, ) __VA_ARGS__)
#define MLG_WARN(...) MLG_LocalLogger().Log(Log::Level::Warn, std::source_location::current()__VA_OPT__(, ) __VA_ARGS__)
#define MLG_ERROR(...) MLG_LocalLogger().Log(Log::Level::Error, std::source_location::current()__VA_OPT__(, ) __VA_ARGS__)