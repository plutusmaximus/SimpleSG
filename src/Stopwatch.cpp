#include "Stopwatch.h"

#include <SDL3/SDL.h>

namespace
{
inline uint64_t GetPerfFrequency()
{
    return SDL_GetPerformanceFrequency();
}

inline std::uint64_t GetPerfTime()
{
    return SDL_GetPerformanceCounter();
}
} // namespace

Stopwatch::Stopwatch()
    : m_LastTime(GetPerfTime())
{
}

float
Stopwatch::Mark()
{
    const auto curTime = GetPerfTime();
    const auto ticks = curTime - m_LastTime;
    m_LastTime = curTime;
    return static_cast<float>(ticks) / static_cast<float>(GetPerfFrequency());
}

float
Stopwatch::ElapsedSeconds() const
{
    const auto curTime = GetPerfTime();
    const auto ticks = curTime - m_LastTime;
    return static_cast<float>(ticks) / static_cast<float>(GetPerfFrequency());
}