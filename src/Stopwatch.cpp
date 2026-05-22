#include "Stopwatch.h"

#include <SDL3/SDL.h>

static inline uint64_t GetFrequency()
{
    static uint64_t freq = 0;
    if (freq == 0)
    {
        freq = SDL_GetPerformanceFrequency();
    }
    return freq;
}

Stopwatch::Stopwatch()
{
    m_LastTime = SDL_GetPerformanceCounter();
}

float
Stopwatch::Mark()
{
    const auto curTime = SDL_GetPerformanceCounter();
    const auto ticks = curTime - m_LastTime;
    m_LastTime = curTime;
    return static_cast<float>(ticks) / static_cast<float>(GetFrequency());
}

float
Stopwatch::ElapsedSeconds() const
{
    const auto curTime = SDL_GetPerformanceCounter();
    const auto ticks = curTime - m_LastTime;
    return static_cast<float>(ticks) / static_cast<float>(GetFrequency());
}