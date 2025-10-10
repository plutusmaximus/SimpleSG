#include "Assert.h"

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