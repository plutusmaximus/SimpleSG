#include "Stopwatch.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static inline std::int64_t GetFrequency()
{
    static std::int64_t freq = 0;
    if (freq == 0)
    {
        LARGE_INTEGER frequency;
        QueryPerformanceFrequency(&frequency);
        freq = frequency.QuadPart;
    }
    return freq;
}

Stopwatch::Stopwatch()
{
    LARGE_INTEGER curTime;
    QueryPerformanceCounter(&curTime);
    m_LastTime = curTime.QuadPart;
}

float
Stopwatch::Mark()
{
    LARGE_INTEGER curTime;
    QueryPerformanceCounter(&curTime);
    const auto ticks = curTime.QuadPart - m_LastTime;
    m_LastTime = curTime.QuadPart;
    return ticks / static_cast<float>(GetFrequency());
}

float
Stopwatch::Elapsed()
{
    LARGE_INTEGER curTime;
    QueryPerformanceCounter(&curTime);
    const auto ticks = curTime.QuadPart - m_LastTime;
    return ticks / static_cast<float>(GetFrequency());
}