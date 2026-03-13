#include "Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/dist_sink.h>

#include <vector>

static std::shared_ptr<spdlog::sinks::dist_sink_mt> mux_sink;

static std::atomic<bool> s_InitializeSinks = true;

static void InitializeSinks()
{
    if (s_InitializeSinks.exchange(false))
    {
        auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        mux_sink = std::make_shared<spdlog::sinks::dist_sink_mt>();

        mux_sink->add_sink(consoleSink);
        mux_sink->add_sink(msvcSink);

        consoleSink->set_level(spdlog::level::debug);
        msvcSink->set_level(spdlog::level::debug);
        mux_sink->set_level(spdlog::level::debug);
    }
}

Log::Logger::Logger(const std::string_view name)
{
    static_assert(sizeof(m_Buffer) >= sizeof(std::shared_ptr<spdlog::logger>));

    [[maybe_unused]] const size_t size = sizeof(std::shared_ptr<spdlog::logger>);

    InitializeSinks();

    auto logger = std::make_shared<spdlog::logger>(std::string(name), mux_sink);

    ::new(m_Buffer) std::shared_ptr<spdlog::logger>(std::move(logger));
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
    static_assert(std::underlying_type_t<Log::Level>(Log::Level::Trace) == spdlog::level::trace);
    static_assert(std::underlying_type_t<Log::Level>(Log::Level::Debug) == spdlog::level::debug);
    static_assert(std::underlying_type_t<Log::Level>(Log::Level::Info) == spdlog::level::info);
    static_assert(std::underlying_type_t<Log::Level>(Log::Level::Warn) == spdlog::level::warn);
    static_assert(std::underlying_type_t<Log::Level>(Log::Level::Error) == spdlog::level::err);

    auto logger = reinterpret_cast<std::shared_ptr<spdlog::logger>*>(m_Buffer);

    (*logger)->set_level(static_cast<spdlog::level::level_enum>(level));
}

void
Log::SetLevel(const Level level)
{
    static_assert(std::underlying_type_t<Log::Level>(Log::Level::Trace) == spdlog::level::trace);
    static_assert(std::underlying_type_t<Log::Level>(Log::Level::Debug) == spdlog::level::debug);
    static_assert(std::underlying_type_t<Log::Level>(Log::Level::Info) == spdlog::level::info);
    static_assert(std::underlying_type_t<Log::Level>(Log::Level::Warn) == spdlog::level::warn);
    static_assert(std::underlying_type_t<Log::Level>(Log::Level::Error) == spdlog::level::err);

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

Log::Capture::~Capture()
{
    if (!m_Canceled)
    {
        Cancel();
    }

    auto sinkPtr = reinterpret_cast<spdlog::sink_ptr*>(m_SinkBuffer);
    sinkPtr->~shared_ptr<spdlog::sinks::sink>();
}

Log::Capture::Capture()
{
    static const size_t s_SinkSize = sizeof(spdlog::sink_ptr);
    static_assert(s_SinkSize <= sizeof(m_SinkBuffer));

    InitializeSinks();

    auto sink = std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(1);
    auto sp = ::new (m_SinkBuffer) spdlog::sink_ptr(std::move(sink));

    //Add a ring buffer sink to capture the assert messages
    mux_sink->add_sink(*sp);
}

void
Log::Capture::Cancel()
{
    mux_sink->remove_sink(*reinterpret_cast<spdlog::sink_ptr*>(m_SinkBuffer));
    m_Canceled = true;
}

bool
Log::Capture::IsCanceled() const
{
    return m_Canceled;
}

std::string
Log::Capture::Message() const
{
    auto sinkPtr = reinterpret_cast<const spdlog::sink_ptr*>(m_SinkBuffer);
    auto sink = static_cast<spdlog::sinks::ringbuffer_sink_mt*>(sinkPtr->get());
    const auto message =
        sink->last_formatted().empty()
        ? std::string()
        : sink->last_formatted().back();

    return message;
}