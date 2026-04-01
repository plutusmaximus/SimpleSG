#define __LOGGER_NAME__ "PERF"

#include "PerfMetrics.h"

#include "Log.h"
#include "PoolAllocator.h"

#include <mutex>
#include <unordered_map>

#include <SDL3/SDL.h>

static std::mutex s_TimerMutex;

static bool s_IsFrameActive = false;

inlist<PerfTimer, &PerfTimer::m_ListNode> PerfMetrics::m_Timers;

static inline std::uint64_t GetPerfFrequency()
{
    return SDL_GetPerformanceFrequency();
}

static inline std::uint64_t GetPerfTime()
{
    return SDL_GetPerformanceCounter();
}

PerfTimer::PerfTimer(const imstring& name)
    : m_Name(name)
{
    std::lock_guard lock(s_TimerMutex);
    PerfMetrics::m_Timers.push_back(this);
}

float
PerfTimer::PerfTimer::GetValue() const
{
    return m_ElapsedSum / NUM_SAMPLES / static_cast<float>(GetPerfFrequency());
}

unsigned
PerfTimer::PerfTimer::GetCount() const
{
    return static_cast<unsigned>(m_CountSum / NUM_SAMPLES);
}

void
PerfTimer::PerfTimer::Start()
{
    if(!MLG_VERIFY(!m_IsRunning,
        "Failed to start timer '{}': Timer is already running", m_Name))
    {
        MLG_ERROR("Failed to start timer '{}': Timer is already running", m_Name);
        return;
    }

    m_StartTime = GetPerfTime();
    ++m_Count;
    m_IsRunning = true;
}

void
PerfTimer::PerfTimer::Stop()
{
    if(!MLG_VERIFY(m_IsRunning,
        "Failed to stop timer '{}': Timer is not running", m_Name))
    {
        MLG_ERROR("Failed to stop timer '{}': Timer is not running", m_Name);
        return;
    }

    const uint64_t curTime = GetPerfTime();
    const uint64_t elapsed = curTime - m_StartTime;

    m_Elapsed += elapsed;
    m_IsRunning = false;
}

void
PerfTimer::PerfTimer::Sample()
{
    if(!MLG_VERIFY(!m_IsRunning,
        "Failed to sample timer '{}': Timer is still running", m_Name))
    {
        MLG_ERROR("Failed to sample timer '{}': Timer is still running", m_Name);
        return;
    }

    m_ElapsedSum += m_Elapsed;
    m_CountSum += m_Count;

    m_SampleIndex = (m_SampleIndex + 1) % NUM_SAMPLES;

    m_ElapsedSum -= m_ElapsedSamples[m_SampleIndex];
    m_CountSum -= m_CountSamples[m_SampleIndex];

    m_ElapsedSamples[m_SampleIndex] = m_Elapsed;
    m_CountSamples[m_SampleIndex] = m_Count;

    m_Elapsed = 0;
    m_Count = 0;
    m_IsRunning = false;
}

void
PerfMetrics::BeginFrame()
{
    if(!MLG_VERIFY(!s_IsFrameActive, "BeginFrame() called while a frame is already active"))
    {
        MLG_ERROR("BeginFrame() called while a frame is already active");
        return;
    }

    s_IsFrameActive = true;
}

void
PerfMetrics::EndFrame()
{
    if(!MLG_VERIFY(s_IsFrameActive, "EndFrame() called without a matching BeginFrame()"))
    {
        MLG_ERROR("EndFrame() called without a matching BeginFrame()");
        return;
    }

    s_IsFrameActive = false;

    std::lock_guard lock(s_TimerMutex);

    for(auto& timer : m_Timers)
    {
        timer.Sample();
    }
}

unsigned
PerfMetrics::GetTimerCount()
{
    std::scoped_lock lock(s_TimerMutex);

    return static_cast<unsigned>(m_Timers.size());
}

unsigned
PerfMetrics::GetTimers(TimerStat* outStats, const unsigned timerCount)
{
    std::scoped_lock lock(s_TimerMutex);

    unsigned count = 0;

    for(auto& timer : m_Timers)
    {
        if(count >= timerCount)
        {
            return timerCount;
        }

        outStats[count++] = TimerStat(timer.GetName(), timer.GetValue(), timer.GetCount());
    }

    return count;
}

void
PerfMetrics::LogTimers()
{
    for(auto& timer : m_Timers)
    {
        MLG_INFO("{}: {} ms, Count: {}", timer.GetName(), timer.GetValue() * 1000.0f, timer.GetCount());
    }
}