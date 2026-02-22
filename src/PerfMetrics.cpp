#define __LOGGER_NAME__ "PERF"

#include "PerfMetrics.h"

#include "Logging.h"
#include "PoolAllocator.h"

#include <mutex>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static std::mutex s_TimerMutex;

static bool s_IsFrameActive = false;

inlist<PerfTimer, &PerfTimer::m_ListNode> PerfMetrics::m_Timers;

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

PerfTimer::PerfTimer(const imstring& name)
    : m_Name(name)
{
    std::lock_guard lock(s_TimerMutex);
    PerfMetrics::m_Timers.push_back(this);
}

float
PerfTimer::PerfTimer::GetValue() const
{
    return m_ElapsedSum / NUM_SAMPLES / static_cast<float>(GetFrequency());
}

unsigned
PerfTimer::PerfTimer::GetCount() const
{
    return static_cast<unsigned>(m_CountSum / NUM_SAMPLES);
}

void
PerfTimer::PerfTimer::Start()
{
    if(!everify(!m_IsRunning,
        "Failed to start timer '{}': Timer is already running", m_Name))
    {
        logError("Failed to start timer '{}': Timer is already running", m_Name);
        return;
    }

    LARGE_INTEGER curTime;
    QueryPerformanceCounter(&curTime);
    m_StartTime = curTime.QuadPart;
    ++m_Count;
    m_IsRunning = true;
}

void
PerfTimer::PerfTimer::Stop()
{
    if(!everify(m_IsRunning,
        "Failed to stop timer '{}': Timer is not running", m_Name))
    {
        logError("Failed to stop timer '{}': Timer is not running", m_Name);
        return;
    }

    LARGE_INTEGER curTime;
    QueryPerformanceCounter(&curTime);
    const uint64_t elapsed = curTime.QuadPart - m_StartTime;

    m_Elapsed += elapsed;
    m_IsRunning = false;
}

void
PerfTimer::PerfTimer::Sample()
{
    if(!everify(!m_IsRunning,
        "Failed to sample timer '{}': Timer is still running", m_Name))
    {
        logError("Failed to sample timer '{}': Timer is still running", m_Name);
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
    if(!everify(!s_IsFrameActive, "BeginFrame() called while a frame is already active"))
    {
        logError("BeginFrame() called while a frame is already active");
        return;
    }

    s_IsFrameActive = true;
}

void
PerfMetrics::EndFrame()
{
    if(!everify(s_IsFrameActive, "EndFrame() called without a matching BeginFrame()"))
    {
        logError("EndFrame() called without a matching BeginFrame()");
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
        logInfo("{}: {} ms, Count: {}", timer.GetName(), timer.GetValue() * 1000.0f, timer.GetCount());
    }
}