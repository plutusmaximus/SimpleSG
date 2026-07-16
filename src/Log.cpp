#include "Log.h"

#include "SanitizerHelpers.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/dist_sink.h>

#include <mutex>
#include <vector>

namespace
{

std::shared_ptr<spdlog::sinks::dist_sink_mt>* MakeMuxSink()
{
    auto* muxSink = new std::shared_ptr<spdlog::sinks::dist_sink_mt>(
        std::make_shared<spdlog::sinks::dist_sink_mt>()); // NOLINT(cppcoreguidelines-owning-memory)

    // We intentionally leak this, so hide it from leak sanitizers
    MLG_LSAN_IGNORE_OBJECT(muxSink);

    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    (*muxSink)->add_sink(consoleSink);
    consoleSink->set_level(spdlog::level::debug);

#if defined(_MSC_VER)
    // Logs to the Visual Studio output window when running under the debugger.
    auto msvcSink = std::make_shared<spdlog::sinks::msvc_sink_mt>();
    (*muxSink)->add_sink(msvcSink);
    msvcSink->set_level(spdlog::level::debug);
#endif

    (*muxSink)->set_level(spdlog::level::debug);

    return muxSink;
}

std::shared_ptr<spdlog::sinks::dist_sink_mt>& GetMuxSink()
{
    static std::shared_ptr<spdlog::sinks::dist_sink_mt>* muxSink = MakeMuxSink();
    return *muxSink;
}

std::mutex* MakeMutex()
{
    std::mutex* p = new std::mutex; // NOLINT(cppcoreguidelines-owning-memory)

    // We intentionally leak this, so hide it from leak sanitizers
    MLG_LSAN_IGNORE_OBJECT(p);

    return p;
}

std::mutex& GetMutex()
{
    static std::mutex* mutex = MakeMutex();
    return *mutex;
}

std::vector<std::string>* MakePreficStack()
{
    std::vector<std::string>* p = new std::vector<std::string>; // NOLINT(cppcoreguidelines-owning-memory)

    // We intentionally leak this, so hide it from leak sanitizers
    MLG_LSAN_IGNORE_OBJECT(p);

    return p;
}

// Keep these thread_local values behind function scope so each thread still gets
// its own prefix state without exposing namespace-scope thread_local objects.
// Otherwise MSVC can emit C5046 (__tlregdtor) for internal-linkage thread_local state.
std::vector<std::string>& GetPrefixStack()
{
    static thread_local std::vector<std::string>* logPrefixStack = MakePreficStack();
    return *logPrefixStack;
}

bool& ShouldRebuildPrefix()
{
    static thread_local bool shouldRebuildPrefix = false;
    return shouldRebuildPrefix;
}

std::string& LogPrefix()
{
    static thread_local std::string logPrefix;

    if(ShouldRebuildPrefix())
    {
        logPrefix = "[";

        int count = 0;

        for(const auto& component : GetPrefixStack())
        {
            if(count > 0)
            {
                logPrefix += " : ";
            }
            logPrefix += component;
            ++count;
        }

        logPrefix += "] ";
        ShouldRebuildPrefix() = false;
    }
    return logPrefix;
}

std::shared_ptr<spdlog::logger> GetLogger(const std::string& name)
{
    const std::lock_guard<std::mutex> lock(GetMutex());

    std::shared_ptr<spdlog::logger> logger = spdlog::get(name);

    if(!logger)
    {
        logger = std::make_shared<spdlog::logger>(name, GetMuxSink());

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
    GetPrefixStack().push_back(message);
    ShouldRebuildPrefix() = true;
}

void
Log::PopPrefix()
{
    if(!GetPrefixStack().empty())
    {
        GetPrefixStack().pop_back();
        ShouldRebuildPrefix() = true;
    }
}

std::string
Log::Prefix(const std::string& message)
{
    return LogPrefix() + message;
}