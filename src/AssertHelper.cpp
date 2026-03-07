#include "AssertHelper.h"

#include "Log.h"

#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else	//_MSC_VER
#error "Platform not supported"
#endif	//_MSC_VER

#include <atomic>
#include <stacktrace>

static std::atomic<bool> s_EnableAssertDialog = true;

bool
AssertHelper::SetDialogEnabled(const bool enabled)
{
    return s_EnableAssertDialog.exchange(enabled);
}

#if defined(_MSC_VER)

bool
AssertHelper::Log(const std::string& message, bool& mute)
{
    auto trace = std::stacktrace::current(1);
    std::string logMsg = std::format("{}\n\n{}", message, std::to_string(trace));

    Log::Assert(logMsg);

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