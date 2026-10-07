#include "Log.h"

#include "InplaceString.h"
#include "SanitizerHelpers.h"

#include <mutex>
#include <span>
#include <spdlog/sinks/dist_sink.h>
#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>
#include <string_view>

namespace
{

struct LogState
{
    std::mutex Mutex;
    std::shared_ptr<spdlog::sinks::dist_sink_mt> MuxSink;
};

constexpr char kPrefixSeparator[] = { ' ', ':', ' ' };

constexpr size_t kMaxPrefixStackSize = 16;
constexpr size_t kMaxPrefixComponentLen = 127;
constexpr size_t kMaxPrefixBufferSize =
    ((kMaxPrefixComponentLen + std::size(kPrefixSeparator)) * kMaxPrefixStackSize)
    + 1; // +1 for null terminator

using PrefixComponentString = InplaceString<kMaxPrefixComponentLen>;

struct ThreadLogState // NOLINT(cppcoreguidelines-pro-type-member-init)
{
    PrefixComponentString PrefixStack[kMaxPrefixStackSize];
    size_t PushDepth = 0;
    char PrefixBuffer[kMaxPrefixBufferSize];
    std::string_view Prefix;
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
    static thread_local ThreadLogState* threadLogState{ new ThreadLogState };

    // We intentionally leak this, so hide it from leak sanitizers
    MLG_LSAN_IGNORE_OBJECT(threadLogState);

    return *threadLogState;
}

Log::Logger&
GetAssertLogger()
{
    static Log::Logger* assertLogger{ new Log::Logger("ASSERT") };

    // We intentionally leak this, so hide it from leak sanitizers
    MLG_LSAN_IGNORE_OBJECT(assertLogger);

    return *assertLogger;
}

std::string_view
LogPrefix()
{
    ThreadLogState& threadLogState = GetThreadLogState();
    if(threadLogState.ShouldRebuildPrefix)
    {
        char* prefixBuffer = &threadLogState.PrefixBuffer[0];
        std::span<char> appender = std::span(prefixBuffer, kMaxPrefixBufferSize);

        size_t prefixLength = 0;

        const size_t stackDepth = std::min(threadLogState.PushDepth, kMaxPrefixStackSize);
        const std::span prefixStack(&threadLogState.PrefixStack[0], stackDepth);

        for(const auto& component : prefixStack)
        {
            if(prefixLength > 0)
            {
                if(appender.size() < std::size(kPrefixSeparator))
                {
                    break;
                }

                std::copy_n(&kPrefixSeparator[0], std::size(kPrefixSeparator), appender.data());

                appender = appender.subspan(std::size(kPrefixSeparator));
                prefixLength += std::size(kPrefixSeparator);
            }

            const size_t sizeToAppend = std::min(appender.size(), component.size());

            std::copy_n(component.data(), sizeToAppend, appender.data());
            appender = appender.subspan(sizeToAppend);
            prefixLength += sizeToAppend;
        }

        threadLogState.Prefix = std::string_view(prefixBuffer, prefixLength);
        threadLogState.ShouldRebuildPrefix = false;
    }

    return threadLogState.Prefix;
}

std::shared_ptr<spdlog::logger>
GetLogger(const std::string_view name)
{
    const std::lock_guard<std::mutex> lock(GetLogState().Mutex);

    std::string loggerName(name);

    std::shared_ptr<spdlog::logger> logger = spdlog::get(loggerName);

    if(!logger)
    {
        logger = std::make_shared<spdlog::logger>(std::move(loggerName), GetLogState().MuxSink);

        spdlog::initialize_logger(logger);
        spdlog::register_or_replace(logger);
    }

    return logger;
}
} // namespace

Log::Logger::Logger(const std::string_view name)
    : m_Logger(GetLogger(name))
{
}

void
Log::Logger::Log(
    const Level level, const std::source_location& srcLoc, const std::string_view message)
{
    char buffer[kMaxSizeofFormatBuffer];

    constexpr size_t kMaxPrefixSize = std::size(buffer) / 2;
    constexpr const char ellipsis[] = { '.', '.', '.' };

    char logPrefixBuffer[kMaxPrefixSize + std::size(ellipsis)];

    std::string_view logPrefix = LogPrefix();

    if(logPrefix.size() > kMaxPrefixSize)
    {
        // Prefix is too long.  Truncate the prefix and add an ellipsis.
        logPrefix = logPrefix.substr(0, kMaxPrefixSize);

        // Copy the truncated prefix and the ellipsis into the log prefix buffer.
        std::copy_n(logPrefix.data(), logPrefix.size(), &logPrefixBuffer[0]);
        std::copy_n(&ellipsis[0], std::size(ellipsis), &logPrefixBuffer[logPrefix.size()]);

        // Update the log prefix to include the truncated prefix and the ellipsis.
        logPrefix = std::string_view(&logPrefixBuffer[0], std::size(logPrefixBuffer));
    }

    if(Level::Error == level)
    {
        // For errors include the source location in the log message.
        const std::string_view formattedMsg = FormatToBuffer(buffer,
            "[{}] {}({}): {} - {}",
            logPrefix,
            srcLoc.file_name(),
            srcLoc.line(),
            srcLoc.function_name(),
            message);

        LogImpl(level, formattedMsg);
    }
    else
    {
        const std::string_view formattedMsg =
            FormatToBuffer(buffer, "[{}] {}", logPrefix, message);

        LogImpl(level, formattedMsg);
    }
}

void
Log::Logger::LogImpl(const Level level, const std::string_view message)
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
Log::LogAssert(const std::source_location& srcLoc, const std::string_view message)
{
    GetAssertLogger().Log(Log::Level::Error, srcLoc, message);
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
Log::PushPrefix(const std::string_view message)
{
    ThreadLogState& threadLogState = GetThreadLogState();

    if(threadLogState.PushDepth < kMaxPrefixStackSize)
    {
        std::string_view truncatedMsg = message;
        char truncateBuf[PrefixComponentString::kMaxLength + 1]; // +1 for null terminator

        if(message.size() > PrefixComponentString::kMaxLength)
        {
            // If the prefix component is too long, truncate it and append an ellipsis.

            constexpr const char kEllipsis[] = { '.', '.', '.' };
            constexpr size_t kSizeToCopy = PrefixComponentString::kMaxLength - std::size(kEllipsis);

            std::span<char> bufSpan(truncateBuf);
            const char* src = message.data();
            std::copy_n(src, kSizeToCopy, bufSpan.data());

            bufSpan = bufSpan.subspan(kSizeToCopy);
            std::copy_n(&kEllipsis[0], std::size(kEllipsis), bufSpan.data());

            truncatedMsg = std::string_view(&truncateBuf[0], PrefixComponentString::kMaxLength);
        }

        threadLogState.PrefixStack[threadLogState.PushDepth] = truncatedMsg;
        threadLogState.ShouldRebuildPrefix = true;
    }

    // Increment the push count even if nothing was pushed.
    // Then PopPrefix can correctly decrement the push count.
    ++threadLogState.PushDepth;
}

void
Log::PopPrefix()
{
    ThreadLogState& threadLogState = GetThreadLogState();
    if(threadLogState.PushDepth > 0)
    {
        --threadLogState.PushDepth;

        if(threadLogState.PushDepth < kMaxPrefixStackSize)
        {
            // Only rebuild the prefix if the push count is within the maximum limit.
            threadLogState.ShouldRebuildPrefix = true;
        }
    }
}