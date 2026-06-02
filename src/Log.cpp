#include "Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/dist_sink.h>

#include <mutex>
#include <vector>

namespace
{

struct LogGlobals
{
    static inline std::atomic<bool> InitializeSinks = true;
    static inline std::shared_ptr<spdlog::sinks::dist_sink_mt> MuxSink;
    static inline std::mutex LoggerMutex;
    static inline thread_local std::vector<std::string> LogPrefixStack;
    static inline thread_local std::string LogPrefix;
};

void InitializeSinks()
{
    const std::lock_guard<std::mutex> lock(LogGlobals::LoggerMutex);

    if (LogGlobals::InitializeSinks.exchange(false))
    {
        LogGlobals::MuxSink = std::make_shared<spdlog::sinks::dist_sink_mt>();

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        LogGlobals::MuxSink->add_sink(consoleSink);
        consoleSink->set_level(spdlog::level::debug);

#if defined(_MSC_VER)
        // Logs to the Visual Studio output window when running under the debugger.
        auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        LogGlobals::MuxSink->add_sink(msvcSink);
        msvcSink->set_level(spdlog::level::debug);
#endif

        LogGlobals::MuxSink->set_level(spdlog::level::debug);
    }
}

std::shared_ptr<spdlog::logger> GetLogger(const std::string& name)
{
    InitializeSinks();

    const std::lock_guard<std::mutex> lock(LogGlobals::LoggerMutex);

    std::shared_ptr<spdlog::logger> logger = spdlog::get(name);

    if(!logger)
    {
        logger = std::make_shared<spdlog::logger>(name, LogGlobals::MuxSink);

        spdlog::initialize_logger(logger);
        spdlog::register_or_replace(logger);
    }

    return logger;
}
} // namespace

Log::Logger::Logger(const std::string& name)
    : m_Logger(GetLogger(name))
{
}

void Log::Logger::LogImpl(const Level level, const std::string& message)
{
    switch(level)
    {
        case Log::Level::Trace: m_Logger->trace(message); break;
        case Log::Level::Debug: m_Logger->debug(message); break;
        case Log::Level::Info: m_Logger->info(message); break;
        case Log::Level::Warn: m_Logger->warn(message); break;
        case Log::Level::Error: m_Logger->error(message); break;
    }
}

void Log::Logger::SetLevel(const Level level)
{
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Trace) == spdlog::level::trace);
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Debug) == spdlog::level::debug);
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Info) == spdlog::level::info);
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Warn) == spdlog::level::warn);
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Error) == spdlog::level::err);

    m_Logger->set_level(static_cast<spdlog::level::level_enum>(level));
}

void
Log::SetLevel(const Level level)
{
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Trace) == spdlog::level::trace);
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Debug) == spdlog::level::debug);
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Info) == spdlog::level::info);
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Warn) == spdlog::level::warn);
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Error) == spdlog::level::err);

    spdlog::set_level(static_cast<spdlog::level::level_enum>(level));
}

void
Log::PushPrefix(const std::string& message)
{
    LogGlobals::LogPrefixStack.push_back(message);
    MakePrefix();
}

void
Log::PopPrefix()
{
    if(!LogGlobals::LogPrefixStack.empty())
    {
        LogGlobals::LogPrefixStack.pop_back();
        MakePrefix();
    }
}

void
Log::MakePrefix()
{
    std::string& prefix = LogGlobals::LogPrefix;
    prefix.clear();
    prefix += "[";

    int count = 0;

    for(const auto& component : LogGlobals::LogPrefixStack)
    {
        if(count > 0)
        {
            prefix += " : ";
        }
        prefix += component;
        ++count;
    }

    prefix += "] ";
}

std::string
Log::Prefix(const std::string& message)
{
    return LogGlobals::LogPrefix + message;
}