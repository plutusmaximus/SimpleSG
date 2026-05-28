#include "Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/dist_sink.h>

#include <mutex>
#include <vector>

static std::shared_ptr<spdlog::sinks::dist_sink_mt> mux_sink;

static std::atomic<bool> s_InitializeSinks = true;

static std::mutex s_LoggerMutex;

static void InitializeSinks()
{
    const std::lock_guard<std::mutex> lock(s_LoggerMutex);

    if (s_InitializeSinks.exchange(false))
    {
        mux_sink = std::make_shared<spdlog::sinks::dist_sink_mt>();

        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        mux_sink->add_sink(consoleSink);
        consoleSink->set_level(spdlog::level::debug);

#if defined(_MSC_VER)
        // Logs to the Visual Studio output window when running under the debugger.
        auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        mux_sink->add_sink(msvcSink);
        msvcSink->set_level(spdlog::level::debug);
#endif

        mux_sink->set_level(spdlog::level::debug);
    }
}

Log::Logger::Logger(const std::string& name)
{
    static_assert(sizeof(m_Buffer) >= sizeof(std::shared_ptr<spdlog::logger>));

    InitializeSinks();

    const std::lock_guard<std::mutex> lock(s_LoggerMutex);

    std::shared_ptr<spdlog::logger> logger = spdlog::get(name);

    if(!logger)
    {
        logger = std::make_shared<spdlog::logger>(name, mux_sink);

        spdlog::initialize_logger(logger);
        spdlog::register_or_replace(logger);
    }

    ::new(m_Buffer) std::shared_ptr<spdlog::logger>(logger);
}

Log::Logger::~Logger()
{
    auto logger = reinterpret_cast<std::shared_ptr<spdlog::logger>*>(m_Buffer);
    logger->~shared_ptr();
}

void Log::Logger::LogImpl(const Level level, const std::string& message)
{
    auto logger = reinterpret_cast<std::shared_ptr<spdlog::logger>*>(m_Buffer);

    switch(level)
    {
        case Log::Level::Trace: (*logger)->trace(message); break;
        case Log::Level::Debug: (*logger)->debug(message); break;
        case Log::Level::Info: (*logger)->info(message); break;
        case Log::Level::Warn: (*logger)->warn(message); break;
        case Log::Level::Error: (*logger)->error(message); break;
    }
}

void Log::Logger::SetLevel(const Level level)
{
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Trace) == spdlog::level::trace);
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Debug) == spdlog::level::debug);
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Info) == spdlog::level::info);
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Warn) == spdlog::level::warn);
    static_assert(static_cast<std::underlying_type_t<Log::Level>>(Log::Level::Error) == spdlog::level::err);

    auto logger = reinterpret_cast<std::shared_ptr<spdlog::logger>*>(m_Buffer);

    (*logger)->set_level(static_cast<spdlog::level::level_enum>(level));
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

static thread_local std::vector<std::string> s_LogPrefixStack;
static thread_local std::string s_LogPrefix;

void
Log::PushPrefix(const std::string& message)
{
    s_LogPrefixStack.push_back(message);
    MakePrefix();
}

void
Log::PopPrefix()
{
    if(!s_LogPrefixStack.empty())
    {
        s_LogPrefixStack.pop_back();
        MakePrefix();
    }
}

void
Log::MakePrefix()
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

std::string
Log::Prefix(const std::string& message)
{
    return s_LogPrefix + message;
}