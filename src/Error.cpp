#include "Error.h"

#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else	//_MSC_VER
#error "Platform not supported"
#endif	//_MSC_VER

#include <string>
#include <stacktrace>
#include <cstdarg>

#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/dist_sink.h>

static std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console_sink;
static std::shared_ptr<spdlog::sinks::msvc_sink_mt> msvc_sink;
static std::shared_ptr<spdlog::sinks::dist_sink_mt> mux_sink;

static std::atomic<bool> s_InitializeSinks = true;

static void InitializeSinks()
{
    if (s_InitializeSinks.exchange(false))
    {
        console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
        mux_sink = std::make_shared<spdlog::sinks::dist_sink_mt>();

        mux_sink->add_sink(console_sink);
        mux_sink->add_sink(msvc_sink);

        console_sink->set_level(spdlog::level::debug);
        msvc_sink->set_level(spdlog::level::debug);
        mux_sink->set_level(spdlog::level::debug);
    }
}

// ============== Logging =================

std::shared_ptr<spdlog::logger>
LogHelper::CreateLogger(const std::string_view name)
{
    InitializeSinks();

    auto logger = std::make_shared<spdlog::logger>(std::string(name), mux_sink);

    spdlog::initialize_logger(logger);
    spdlog::register_or_replace(logger);

    return logger;
}

// ============== Asserts =================

static std::atomic<bool> s_EnableAssertDialog = true;

bool
Asserts::SetDialogEnabled(const bool enabled)
{
    return s_EnableAssertDialog.exchange(enabled);
}

Asserts::Capture::Capture()
    : m_OldValue(Asserts::SetDialogEnabled(false))
    , m_Sink(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(1))
{
    InitializeSinks();

    //Add a ring buffer sink to capture the assert messages
    mux_sink->add_sink(m_Sink);
}

void
Asserts::Capture::Cancel()
{
    mux_sink->remove_sink(m_Sink);
    Asserts::SetDialogEnabled(m_OldValue);
    m_Canceled = true;
}

bool
Asserts::Capture::IsCanceled() const
{
    return m_Canceled;
}

std::string
Asserts::Capture::Message() const
{
    auto sink = static_cast<spdlog::sinks::ringbuffer_sink_mt*>(m_Sink.get());
    const auto message =
        sink->last_formatted().empty()
        ? std::string()
        : sink->last_formatted().back();

    return message;
}

Asserts::Capture::~Capture()
{
    if (!m_Canceled)
    {
        Cancel();
    }
}

#if defined(_MSC_VER)

bool
Asserts::Log(const std::string_view message, bool& mute)
{
    auto trace = std::stacktrace::current(1);
    std::string logMsg = std::format("{}\n\n{}", message, std::to_string(trace));

    logAssert("{}", logMsg);

    bool ignore = !s_EnableAssertDialog.load() || mute;
    if (ignore) return false;

    const int msgboxValue = MessageBoxA(
        NULL,
        logMsg.c_str(),
        "Assertion Failed",
        MB_ICONEXCLAMATION | MB_ABORTRETRYIGNORE | MB_DEFBUTTON2);

    if (IDABORT == msgboxValue)
    {
        std::exit(1);
    }

    if(IDIGNORE == msgboxValue)
    {
        mute = true;
    }

    return IDRETRY == msgboxValue;
}

#else	//_MSC_VER
#error "Platform not supported"
#endif	//_MSC_VER