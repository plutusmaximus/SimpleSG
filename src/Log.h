#pragma once

#include <format>
#include <string>

/// Define __LOGGER_NAME__ before including this header to create a logger with a specific name.
/// Otherwise the default logger is used.
/// Example:
/// #define __LOGGER_NAME__ "my_logger"
#ifndef __LOGGER_NAME__
#define __LOGGER_NAME__ "****"
#endif

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

        ~Logger();

        Logger() = delete;
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;

        template<typename... Args>
        inline void Log(const Level level, std::format_string<Args...> fmt, Args&&... args)
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

        uint8_t m_Buffer[16];
    };

    /// Log an assertion failure
    template<typename... Args>
    static inline void Assert(std::format_string<Args...> fmt, Args&&... args)
    {
        s_AssertLogger.Log(Log::Level::Error, std::format(fmt, std::forward<Args>(args)...));
    }

    static inline void Assert(const std::string& message)
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

    /// @brief RAII class to capture messages.
    /// Assert dialogs are disabled while this object is alive.
    /// Usage:
    /// {
    ///     Asserts::Capture capture;
    ///     // code that may trigger asserts
    /// }
    /// If you want to cancel the capture before destruction, call Cancel().
    /// Otherwise, the capture will be canceled automatically in the destructor.
    /// Typically the MLG_ASSERT_CAPTURE macro is used to simplify usage.
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
        ~Capture();

        Capture();

        void Cancel();

        bool IsCanceled() const;

        std::string Message() const;

    private:

        bool m_Canceled = false;

        // Allocate the sink into this buffer so we don't
        // end up with lots of tiny heap allocations.
        uint8_t m_SinkBuffer[16];
    };

private:

    static inline Logger s_AssertLogger{"ASSERT"};

    static void MakePrefix();

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
#define MLG_LOG_SCOPE(...) mlg::LogScope MLG_LOG_SCOPE_CONCAT(logScope_, __LINE__)(__VA_ARGS__);

static inline Log::Logger __logger__(__LOGGER_NAME__);

#define MLG_TRACE(...) __logger__.Log( Log::Level::Trace, __VA_ARGS__)
#define MLG_DEBUG(...) __logger__.Log( Log::Level::Debug, __VA_ARGS__)
#define MLG_INFO(...) __logger__.Log( Log::Level::Info, __VA_ARGS__)
#define MLG_WARN(...) __logger__.Log( Log::Level::Warn, __VA_ARGS__)
#define MLG_ERROR(...) __logger__.Log( Log::Level::Error, __VA_ARGS__)