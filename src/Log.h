#pragma once

#include "imstring.h"

#include <format>
#include <mutex>
#include <string>
#include <vector>

/// Define __LOGGER_NAME__ before including this header to create a logger with a specific name.
/// Otherwise the default logger is used.
/// Example:
/// #define __LOGGER_NAME__ "my_logger"
#ifndef __LOGGER_NAME__
#define __LOGGER_NAME__ "****"
#endif

/// @brief  consteval string label that can be used as a non-type template parameter
/// to specialize loggers by label.
template<std::size_t N>
struct LoggerLabel
{
    char value[N];

    explicit consteval LoggerLabel(const char (&str)[N])
    {
        for(std::size_t i = 0; i < N; ++i)
        {
            value[i] = str[i];
        }
    }

    consteval std::string_view sv() const { return std::string_view(value, N - 1); }
};

static inline constexpr LoggerLabel<sizeof(__LOGGER_NAME__)> __logger_name__{__LOGGER_NAME__};

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

        virtual void Log(const Level level, const std::string& message) = 0;

        virtual void SetLevel(const Level level) = 0;
    };

    template<typename... Args>
    static inline void Trace(std::format_string<Args...> fmt, Args&&... args)
    {
        GetLogger<__logger_name__>()->Log(Log::Level::Trace,
            Prefix(fmt, std::forward<Args>(args)...));
    }

    static inline void Trace(const std::string& message)
    {
        GetLogger<__logger_name__>()->Log(Log::Level::Trace, Prefix(message));
    }

    template<typename... Args>
    static inline void Debug(std::format_string<Args...> fmt, Args&&... args)
    {
        GetLogger<__logger_name__>()->Log(Log::Level::Debug,
            Prefix(fmt, std::forward<Args>(args)...));
    }

    static inline void Debug(const std::string& message)
    {
        GetLogger<__logger_name__>()->Log(Log::Level::Debug, Prefix(message));
    }

    template<typename... Args>
    static inline void Info(std::format_string<Args...> fmt, Args&&... args)
    {
        GetLogger<__logger_name__>()->Log(Log::Level::Info,
            Prefix(fmt, std::forward<Args>(args)...));
    }

    static inline void Info(const std::string& message)
    {
        GetLogger<__logger_name__>()->Log(Log::Level::Info, Prefix(message));
    }

    template<typename... Args>
    static inline void Warn(std::format_string<Args...> fmt, Args&&... args)
    {
        GetLogger<__logger_name__>()->Log(Log::Level::Warn,
            Prefix(fmt, std::forward<Args>(args)...));
    }

    static inline void Warn(const std::string& message)
    {
        GetLogger<__logger_name__>()->Log(Log::Level::Warn, Prefix(message));
    }

    template<typename... Args>
    static inline void Error(std::format_string<Args...> fmt, Args&&... args)
    {
        GetLogger<__logger_name__>()->Log(Log::Level::Error,
            Prefix(fmt, std::forward<Args>(args)...));
    }

    static inline void Error(const std::string& message)
    {
        GetLogger<__logger_name__>()->Log(Log::Level::Error, Prefix(message));
    }

    static inline constexpr LoggerLabel<sizeof("assert")> __assert_logger_name__{"assert"};

    /// Log an assertion failure
    template<typename... Args>
    static inline void Assert(std::format_string<Args...> fmt, Args&&... args)
    {
        GetLogger<__assert_logger_name__>()->Log(Log::Level::Error, Prefix(fmt, std::forward<Args>(args)...));
    }

    static inline void Assert(const std::string& message)
    {
        GetLogger<__assert_logger_name__>()->Log(Log::Level::Error, Prefix(message));
    }

    /// @brief Sets the log level for a specific logger.
    template<LoggerLabel S>
    static inline void SetLevel(const Level level)
    {
        GetLogger<S>()->SetLevel(level);
    }

    /// @brief Sets the global log level.
    static void SetLevel(const Level level);

    template<typename... Args>
    static void PushPrefix(std::format_string<Args...> fmt, Args&&... args)
    {
        s_LogPrefixStack.push_back(std::format(fmt, std::forward<Args>(args)...));
        MakePrefix();
    }

    static void PushPrefix(const std::string& message)
    {
        s_LogPrefixStack.push_back(message);
        MakePrefix();
    }

    static void PopPrefix()
    {
        if(!s_LogPrefixStack.empty())
        {
            s_LogPrefixStack.pop_back();
            MakePrefix();
        }
    }

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
    struct LoggerBuffer
    {
        ~LoggerBuffer()
        {
            if (Deleter)
            {
                Deleter(m_Logger);
            }
        }

        // Calls the destructor for the internal logger.
        void (*Deleter)(Logger*);

        // Allocate the internal logger into this buffer so
        // we don't end up with heap allocations hanging around
        // as static variables don't get destroyed until program exit.
        uint8_t m_Buffer[32];
        Logger* m_Logger = reinterpret_cast<Logger*>(m_Buffer);
    };

    static Logger* CreateLogger(const std::string_view name, LoggerBuffer& buffer);

    /// @brief Global instance of a logger specialized by label.
    template<LoggerLabel S>
    static inline Logger* GetLogger()
    {
        static LoggerBuffer s_LoggerBuffer;
        static Logger* logger = CreateLogger(S.sv(), s_LoggerBuffer);

        return logger;
    }

    static void MakePrefix()
    {
        s_LogPrefix.clear();
        s_LogPrefix += "[";

        int count = 0;

        for(const auto& prefix : s_LogPrefixStack)
        {
            if(count > 0)
            {
                s_LogPrefix += " : ";
            }
            s_LogPrefix += prefix;
            ++count;
        }

        s_LogPrefix += "] ";
    }

    template<typename... Args>
    static std::string Prefix(std::format_string<Args...> fmt, Args&&... args)
    {
        return s_LogPrefix + std::format(fmt, std::forward<Args>(args)...);
    }

    static std::string Prefix(const std::string& message)
    {
        return s_LogPrefix + message;
    }

    static thread_local std::vector<std::string> s_LogPrefixStack;
    static thread_local std::string s_LogPrefix;
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