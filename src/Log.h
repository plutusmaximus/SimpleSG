#pragma once

#include "imstring.h"

#include <format>
#include <string>
#include <string_view>

/// Define __LOGGER_NAME__ before including this header to create a logger with a specific name.
/// Otherwise the default logger is used.
/// Example:
/// #define __LOGGER_NAME__ "my_logger"
#ifndef __LOGGER_NAME__
#define __LOGGER_NAME__ "****"
#endif

class Log final
{
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

        virtual void trace(const std::string& message) = 0;
        virtual void debug(const std::string& message) = 0;
        virtual void info(const std::string& message) = 0;
        virtual void warn(const std::string& message) = 0;
        virtual void error(const std::string& message) = 0;

        virtual void trace(const std::string_view message) = 0;
        virtual void debug(const std::string_view message) = 0;
        virtual void info(const std::string_view message) = 0;
        virtual void warn(const std::string_view message) = 0;
        virtual void error(const std::string_view message) = 0;

        virtual void SetLevel(Level level) = 0;
    };

    template<typename... Args>
    static inline void Trace(std::format_string<Args...> fmt, Args &&...args)
    {
        GetLogger<__LOGGER_NAME__>()->trace(std::format(fmt, std::forward<Args>(args)...));
    }

    static inline void Trace(std::string_view message) { GetLogger<__LOGGER_NAME__>()->trace(message); }

    template<typename... Args>
    static inline void Debug(std::format_string<Args...> fmt, Args &&...args)
    {
        GetLogger<__LOGGER_NAME__>()->debug(std::format(fmt, std::forward<Args>(args)...));
    }

    static inline void Debug(std::string_view message)
    {
        GetLogger<__LOGGER_NAME__>()->debug(message);
    }

    template<typename... Args>
    static inline void Info(std::format_string<Args...> fmt, Args &&...args)
    {
        GetLogger<__LOGGER_NAME__>()->info(std::format(fmt, std::forward<Args>(args)...));
    }

    static inline void Info(std::string_view message) { GetLogger<__LOGGER_NAME__>()->info(message); }

    template<typename... Args>
    static inline void Warn(std::format_string<Args...> fmt, Args &&...args)
    {
        GetLogger<__LOGGER_NAME__>()->warn(std::format(fmt, std::forward<Args>(args)...));
    }

    static inline void Warn(std::string_view message) { GetLogger<__LOGGER_NAME__>()->warn(message); }

    template<typename... Args>
    static inline void Error(std::format_string<Args...> fmt, Args &&...args)
    {
        GetLogger<__LOGGER_NAME__>()->error(std::format(fmt, std::forward<Args>(args)...));
    }

    static inline void Error(std::string_view message) { GetLogger<__LOGGER_NAME__>()->error(message); }

    /// Log an assertion failure
    template<typename... Args>
    static inline void Assert(std::format_string<Args...> fmt, Args &&...args)
    {
        GetLogger<"assert">()->error(std::format(fmt, std::forward<Args>(args)...));
    }

    static inline void Assert(std::string_view message) { GetLogger<"assert">()->error(message); }

    /// @brief Sets the log level for a specific logger.
    template<LoggerLabel S>
    static inline void SetLevel(const Level level)
    {
        GetLogger<S>()->SetLevel(level);
    }

    /// @brief Sets the global log level.
    static void SetLevel(const Level level);

    /// @brief RAII class to capture messages.
    /// Assert dialogs are disabled while this object is alive.
    /// Usage:
    /// {
    ///     Asserts::Capture capture;
    ///     // code that may trigger asserts
    /// }
    /// If you want to cancel the capture before destruction, call Cancel().
    /// Otherwise, the capture will be canceled automatically in the destructor.
    /// Typically the MLG_ASSERT_capture macro is used to simplify usage.
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
        Capture();

        void Cancel();

        bool IsCanceled() const;

        std::string Message() const;

        ~Capture();

    private:

        bool m_Canceled = false;

        uint8_t m_SinkBuffer[16];
    };

private:
    static Logger* CreateLogger(const std::string_view name, uint8_t* buffer, const size_t size);

    /// @brief Global instance of a logger specialized by label.
    template<LoggerLabel S>
    static inline Logger* GetLogger()
    {
        static uint8_t s_LoggerBuffer[32];
        static Logger* logger = CreateLogger(S.sv(), s_LoggerBuffer, std::size(s_LoggerBuffer));

        return logger;
    }
};
