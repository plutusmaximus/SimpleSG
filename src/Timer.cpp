#include "Timer.h"

#include <SDL3/SDL_timer.h>

namespace
{
double
GetInvPerfFrequency()
{
    static const double invPerfFrequency = 1.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    return invPerfFrequency;
}

inline std::uint64_t GetTime()
{
    return SDL_GetPerformanceCounter();
}
} // namespace

void
Timer::Start()
{
    m_Running = true;
    m_StartTime = GetTime();
}

void
Timer::Stop()
{
    if(m_Running)
    {
        m_Running = false;
        m_ElapsedTime += GetTime() - m_StartTime;
    }
}

void
Timer::Clear()
{
    m_StartTime = 0;
    m_ElapsedTime = 0;
}

float
Timer::GetElapsedSeconds() const
{
    const uint64_t elapsed = m_Running ? GetTime() - m_StartTime : m_ElapsedTime;

    return static_cast<float>(elapsed) * static_cast<float>(GetInvPerfFrequency());
}