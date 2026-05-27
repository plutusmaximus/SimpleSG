#include "AssertHelper.h"

#include "Log.h"

#if defined(_MSC_VER)
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#else	//_MSC_VER
//#error "Platform not supported"
#endif	//_MSC_VER

#include <atomic>

#ifndef __clang__
// No stack trace support in clang, so we won't include the header.
#include <stacktrace>
#endif  //__clang__

static std::atomic<bool> s_EnableAssertDialog = true;

bool
AssertHelper::SetDialogEnabled(const bool enabled)
{
    return s_EnableAssertDialog.exchange(enabled);
}

bool
AssertHelper::Log(const std::string& message, bool& mute)
{
#ifdef __clang__
    // No stack trace support in clang, so just log the message.
    const std::string logMsg = std::format("{}", message);
#else
    auto trace = std::stacktrace::current(1);
    const std::string logMsg = std::format("{}\n\n{}", message, std::to_string(trace));
#endif

    Log::Assert(logMsg);

    const bool ignore = !s_EnableAssertDialog.load() || mute;
    if (ignore) return false;

#if defined(_MSC_VER)

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
#else	//_MSC_VER
    return false;
#endif	//_MSC_VER
}
