#include "Error.h"

#include <spdlog/sinks/msvc_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <memory>

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



#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdlib>
#include <cstdio>
#include <string>
#include <stacktrace>

static bool EnableAssertDialog = true;

bool SetAssertDialogEnabled(const bool enabled)
{
    const bool oldValue = EnableAssertDialog;

    EnableAssertDialog = enabled;

    return oldValue;
}


bool ShowAssertDialog(const char* expression, const char* fileName, const int lineNum)
{
    if (!EnableAssertDialog) return false;

    char buf[1024];
    std::snprintf(buf, sizeof(buf) - 1, "%s\nFile:%s\nLine:%d", expression, fileName, lineNum);

    std::string message = buf;
    message += "\n";
    auto trace = std::stacktrace::current();
    message += std::to_string(trace);

    const int msgboxValue = MessageBoxA(
        NULL,
        message.c_str(),
        "Assertion Failed",
        MB_ICONEXCLAMATION | MB_ABORTRETRYIGNORE | MB_DEFBUTTON2);

    if (IDABORT == msgboxValue)
    {
        std::exit(1);
    }

    return IDRETRY == msgboxValue;
}