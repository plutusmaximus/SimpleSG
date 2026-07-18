#include "Log.h"

#include "SanitizerHelpers.h"

#include <mutex>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <vector>

namespace
{

struct LogState
{
    std::mutex Mutex;
    std::shared_ptr<spdlog::sinks::dist_sink_mt> MuxSink;
};

struct ThreadLogState
{
    std::vector<std::string> PrefixStack;
    std::string Prefix;
    bool ShouldRebuildPrefix = false;
};

LogState&
GetLogState()
{
    static auto* state = []
    {
        LogState* logState = new LogState //
            {
                .MuxSink = std::make_shared<spdlog::sinks::dist_sink_mt>(),
            };

        // We intentionally leak this, so hide it from leak sanitizers
        MLG_LSAN_IGNORE_OBJECT(logState);

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        logState->MuxSink->add_sink(consoleSink);
        consoleSink->set_level(spdlog::level::debug);

#if defined(_MSC_VER)
        // Logs to the Visual Studio output window when running under the debugger.
        auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        logState->MuxSink->add_sink(msvcSink);
        msvcSink->set_level(spdlog::level::debug);
#endif

        logState->MuxSink->set_level(spdlog::level::debug);

        return logState;
    }();

    return *state;
}

ThreadLogState&
GetThreadLogState()
{
    static thread_local ThreadLogState* threadLogState = // NOLINT(cppcoreguidelines-owning-memory)
        new ThreadLogState;

    // We intentionally leak this, so hide it from leak sanitizers
    MLG_LSAN_IGNORE_OBJECT(threadLogState);

    return *threadLogState;
}

std::string&
LogPrefix()
{
    static thread_local std::string logPrefix;

    if(GetThreadLogState().ShouldRebuildPrefix)
    {
        logPrefix = "[";

        int count = 0;

        for(const auto& component : GetThreadLogState().PrefixStack)
        {
            if(count > 0)
            {
                logPrefix += " : ";
            }
            logPrefix += component;
            ++count;
        }

        logPrefix += "] ";
        GetThreadLogState().ShouldRebuildPrefix = false;
    }

    return logPrefix;
}

std::shared_ptr<spdlog::logger>
GetLogger(std::string name)
{
    const std::lock_guard<std::mutex> lock(GetLogState().Mutex);

    std::shared_ptr<spdlog::logger> logger = spdlog::get(name);

    if(!logger)
    {
        logger = std::make_shared<spdlog::logger>(std::move(name), GetLogState().MuxSink);

        spdlog::initialize_logger(logger);
        spdlog::register_or_replace(logger);
    }

    return logger;
}
} // namespace

Log::Logger::Logger(std::string name)
    : m_Logger(GetLogger(std::move(name)))
{
}

void
Log::Logger::LogImpl(const Level level, const std::string& message)
{
    switch(level)
    {
        case Log::Level::Trace:
            m_Logger->trace(message);
            break;
        case Log::Level::Debug:
            m_Logger->debug(message);
            break;
        case Log::Level::Info:
            m_Logger->info(message);
            break;
        case Log::Level::Warn:
            m_Logger->warn(message);
            break;
        case Log::Level::Error:
            m_Logger->error(message);
            break;
    }
}

void
Log::Logger::SetLevel(const Level level)
{
    static_assert(
        static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Trace) == spdlog::level::trace);
    static_assert(
        static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Debug) == spdlog::level::debug);
    static_assert(
        static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Info) == spdlog::level::info);
    static_assert(
        static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Warn) == spdlog::level::warn);
    static_assert(
        static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Error) == spdlog::level::err);

    m_Logger->set_level(static_cast<spdlog::level::level_enum>(level));
}

void
Log::SetLevel(const Level level)
{
    static_assert(
        static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Trace) == spdlog::level::trace);
    static_assert(
        static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Debug) == spdlog::level::debug);
    static_assert(
        static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Info) == spdlog::level::info);
    static_assert(
        static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Warn) == spdlog::level::warn);
    static_assert(
        static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Error) == spdlog::level::err);

    spdlog::set_level(static_cast<spdlog::level::level_enum>(level));
}

void
Log::PushPrefix(std::string message)
{
    GetThreadLogState().PrefixStack.push_back(std::move(message));
    GetThreadLogState().ShouldRebuildPrefix = true;
}

void
Log::PopPrefix()
{
    if(!GetThreadLogState().PrefixStack.empty())
    {
        GetThreadLogState().PrefixStack.pop_back();
        GetThreadLogState().ShouldRebuildPrefix = true;
    }
}

std::string
Log::Prefix(const std::string& message)
{
    return LogPrefix() + message;
}