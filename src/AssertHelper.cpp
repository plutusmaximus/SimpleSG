#include "AssertHelper.h"

#include "Log.h"

#ifndef __clang__
// No stack trace support in clang, so we won't include the header.
#include <stacktrace>
#endif  //__clang__

namespace AssertHelper
{
static void Log(const std::string& message);

bool
Log(AssertData& assertData,
    const char* expression,
    const char* function,
    const char* fileName,
    const int lineNum,
    const std::string& userMsg)
{
    const std::string message =
        std::format("{}({}): {} - {}", fileName, lineNum, expression, userMsg);

    Log(message);

    assertData.sdlAssertState =
        SDL_ReportAssertion(&assertData.sdlAssertData, function, fileName, lineNum);

    switch (assertData.sdlAssertState)
    {
        case SDL_ASSERTION_RETRY:
        case SDL_ASSERTION_BREAK:
            return true;

        case SDL_ASSERTION_ABORT:
            std::exit(1);

        case SDL_ASSERTION_IGNORE:
        case SDL_ASSERTION_ALWAYS_IGNORE:
            return false;
    }

    return false;
}

bool
Log(AssertData& assertData,
    const char* expression,
    const char* function,
    const char* fileName,
    const int lineNum)
{
    const std::string message = std::format("{}({}): {}", fileName, lineNum, expression);

    Log(message);

        assertData.sdlAssertState =
        SDL_ReportAssertion(&assertData.sdlAssertData, function, fileName, lineNum);

    switch (assertData.sdlAssertState)
    {
        case SDL_ASSERTION_RETRY:
        case SDL_ASSERTION_BREAK:
            return true;

        case SDL_ASSERTION_ABORT:
            std::exit(1);

        case SDL_ASSERTION_IGNORE:
        case SDL_ASSERTION_ALWAYS_IGNORE:
            return false;
    }

    return false;
}

static void
Log(const std::string& message)
{
#ifdef __clang__
    // No stack trace support in clang, so just log the message.
    const std::string logMsg = std::format("{}", message);
#else
    auto trace = std::stacktrace::current(1);
    const std::string logMsg = std::format("{}\n\n{}", message, std::to_string(trace));
#endif

    Log::Assert(logMsg);
}

}   // namespace AssertHelper