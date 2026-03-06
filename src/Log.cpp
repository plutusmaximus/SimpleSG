#include "Logging.h"

#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/dist_sink.h>

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

std::shared_ptr<spdlog::logger>
Log::CreateLogger(const std::string_view name)
{
    InitializeSinks();

    auto logger = std::make_shared<spdlog::logger>(std::string(name), mux_sink);

    spdlog::initialize_logger(logger);
    spdlog::register_or_replace(logger);

    return logger;
}

Log::Capture::Capture()
    : m_Sink(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(1))
{
    InitializeSinks();

    //Add a ring buffer sink to capture the assert messages
    mux_sink->add_sink(m_Sink);
}

void
Log::Capture::Cancel()
{
    mux_sink->remove_sink(m_Sink);
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
    auto sink = static_cast<spdlog::sinks::ringbuffer_sink_mt*>(m_Sink.get());
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
}