#include "Error.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <string>
#include <stacktrace>

#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

static spdlog::logger& CreateLogger()
{
    static auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    static auto msvc_sink = std::make_shared<spdlog::sinks::msvc_sink_mt>();

    static spdlog::logger logger("logger", { console_sink, msvc_sink });

    logger.set_level(spdlog::level::debug);

    return logger;
}

spdlog::logger&
Logging::GetLogger()
{
    static spdlog::logger& logger = CreateLogger();

    return logger;
}

void
Logging::SetLogLevel(const spdlog::level::level_enum level)
{
    GetLogger().set_level(level);
}

#if defined(_MSC_VER)

static bool EnableAssertDialog = true;

bool SetAssertDialogEnabled(const bool enabled)
{
    const bool oldValue = EnableAssertDialog;

    EnableAssertDialog = enabled;

    return oldValue;
}

bool ShowAssertDialog(const char* expression, const char* fileName, const int lineNum, bool& disableFutureAsserts)
{
    bool ignore = !EnableAssertDialog || disableFutureAsserts;
    if (ignore) return false;

    auto trace = std::stacktrace::current(1);
    std::string message = std::format("{}({}): {}\n\n{}", fileName, lineNum, expression, std::to_string(trace));

    const int msgboxValue = MessageBoxA(
        NULL,
        message.c_str(),
        "Assertion Failed",
        MB_ICONEXCLAMATION | MB_ABORTRETRYIGNORE | MB_DEFBUTTON2);

    if (IDABORT == msgboxValue)
    {
        std::exit(1);
    }

    if(IDIGNORE == msgboxValue)
    {
        disableFutureAsserts = true;
    }

    return IDRETRY == msgboxValue;
}

#else	//_MSC_VER
#error "Platform not supported"
#endif	//_MSC_VER