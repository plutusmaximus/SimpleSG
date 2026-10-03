#include "AssertHelper.h"

#include <SDL3/SDL_init.h>

/*#ifndef __clang__
// No stack trace support in clang, so we won't include the header.
#include <stacktrace>
#endif //__clang__*/

namespace AssertHelper
{

namespace
{
SDL_AssertState
ReportAssertion(SDL_AssertData* data, const char* func, const char* file, int line)
{
    // On many platforms assert dialogs will cause background workers to hang, so we break instead.
    if(!SDL_IsMainThread())
    {
        return SDL_ASSERTION_BREAK;
    }

    return SDL_ReportAssertion(data, func, file, line);
}

void
Log(const std::source_location& srcLoc, const std::string_view message)
{
    // #ifdef __clang__
    //  No stack trace support in clang, so just log the message.
    Log::LogAssert(srcLoc, message);
    /*#else
        auto trace = std::stacktrace::current(1);
        Log::LogAssert(std::format("{}\n\n{}", message, std::to_string(trace)));
    #endif*/
}
} // namespace

bool
Log(AssertData& assertData, const std::string_view expression, const std::source_location& srcLoc)
{
    Log(srcLoc, expression);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunreachable-code"
#endif
    assertData.sdlAssertState = ReportAssertion(&assertData.sdlAssertData,
        srcLoc.function_name(),
        srcLoc.file_name(),
        static_cast<int>(srcLoc.line()));
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    switch(assertData.sdlAssertState)
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
    const std::string_view expression,
    const std::source_location& srcLoc,
    const std::string_view userMsg)
{
    char buffer[kMaxSizeofFormatBuffer];
    const std::string_view formattedMsg =
        Log::FormatToBuffer(buffer, "{} - {}", expression, userMsg);

    Log(srcLoc, formattedMsg);

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunreachable-code"
#endif
    assertData.sdlAssertState = ReportAssertion(&assertData.sdlAssertData,
        srcLoc.function_name(),
        srcLoc.file_name(),
        static_cast<int>(srcLoc.line()));
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

    switch(assertData.sdlAssertState)
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
} // namespace AssertHelper