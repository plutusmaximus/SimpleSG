#include "Log.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/dist_sink.h>

static std::shared_ptr<spdlog::sinks::dist_sink_mt> mux_sink;

static std::atomic<bool> s_InitializeSinks = true;

namespace
{
class SpdLogLogger : public Log::Logger
{
public:

    explicit SpdLogLogger(std::shared_ptr<spdlog::logger>&& logger)
        : m_Logger(std::move(logger))
    {
    }

    void trace(const std::string& message) override
    {
        m_Logger->trace(message);
    }

    void debug(const std::string& message) override
    {
        m_Logger->debug(message);
    }

    void info(const std::string& message) override
    {
        m_Logger->info(message);
    }

    void warn(const std::string& message) override
    {
        m_Logger->warn(message);
    }

    void error(const std::string& message) override
    {
        m_Logger->error(message);
    }

    void trace(const std::string_view message) override
    {
        m_Logger->trace(message);
    }

    void debug(const std::string_view message) override
    {
        m_Logger->debug(message);
    }

    void info(const std::string_view message) override
    {
        m_Logger->info(message);
    }

    void warn(const std::string_view message) override
    {
        m_Logger->warn(message);
    }

    void error(const std::string_view message) override
    {
        m_Logger->error(message);
    }

    void SetLevel(Log::Level level) override
    {
        static_assert(std::underlying_type_t<Log::Level>(Log::Level::Trace) == spdlog::level::trace);
        static_assert(std::underlying_type_t<Log::Level>(Log::Level::Debug) == spdlog::level::debug);
        static_assert(std::underlying_type_t<Log::Level>(Log::Level::Info) == spdlog::level::info);
        static_assert(std::underlying_type_t<Log::Level>(Log::Level::Warn) == spdlog::level::warn);
        static_assert(std::underlying_type_t<Log::Level>(Log::Level::Error) == spdlog::level::err);

        m_Logger->set_level(static_cast<spdlog::level::level_enum>(level));
    }

private:

    std::shared_ptr<spdlog::logger> m_Logger;
};

}

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

Log::Logger*
Log::CreateLogger(const std::string_view name, uint8_t* buffer, const size_t size)
{
    if(size < sizeof(SpdLogLogger))
    {
        return nullptr;
    }

    InitializeSinks();

    auto logger = std::make_shared<spdlog::logger>(std::string(name), mux_sink);

    spdlog::initialize_logger(logger);
    spdlog::register_or_replace(logger);

    return ::new (buffer) SpdLogLogger(std::move(logger));
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

Log::Capture::~Capture()
{
    if (!m_Canceled)
    {
        Cancel();
    }

    auto sinkPtr = reinterpret_cast<spdlog::sink_ptr*>(m_SinkBuffer);
    sinkPtr->~shared_ptr<spdlog::sinks::sink>();
}