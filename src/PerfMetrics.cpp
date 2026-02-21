#define __LOGGER_NAME__ "PERF"

#include "PerfMetrics.h"

#include "Logging.h"
#include "PoolAllocator.h"

#include <mutex>
#include <unordered_map>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

using TimerCollection = std::unordered_map<imstring, PerfMetrics::Timer*>;

namespace
{
    /// @brief Disposes of all timers when the program exits and the allocator is destroyed.
    struct Disposer
    {
        ~Disposer();
    };
}

static PoolAllocator<PerfMetrics::Timer, 256>& GetTimerAllocator()
{
    static PoolAllocator<PerfMetrics::Timer, 256> s_TimerAllocator;
    static Disposer s_Disposer; // Ensures that all timers are deleted when the program exits and the allocator is destroyed.
    return s_TimerAllocator;
}

static TimerCollection& GetTimerCollection()
{
    static TimerCollection s_Timers;
    return s_Timers;
}

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

std::mutex s_TimerMutex;

Disposer::~Disposer()
{
    std::scoped_lock lock(s_TimerMutex);

    TimerCollection& timers = GetTimerCollection();

    for(auto& [name, timer] : timers)
    {
        GetTimerAllocator().Delete(timer);
    }

    timers.clear();
}

PerfMetrics::Timer::Timer(const imstring& name)
    : m_Name(name)
{
}

float
PerfMetrics::Timer::GetValue() const
{
    return static_cast<float>(m_Sum) / NUM_SAMPLES / static_cast<float>(GetFrequency());
}

void
PerfMetrics::Timer::Start()
{
    LARGE_INTEGER curTime;
    QueryPerformanceCounter(&curTime);
    m_StartTime = curTime.QuadPart;
    m_IsRunning = true;
}

void
PerfMetrics::Timer::Stop()
{
    if(m_IsRunning)
    {
        LARGE_INTEGER curTime;
        QueryPerformanceCounter(&curTime);
        m_Elapsed += (curTime.QuadPart - m_StartTime);
        m_IsRunning = false;
    }
}

void
PerfMetrics::Timer::Sample()
{
    m_Sum += m_Elapsed;
    m_SampleIndex = (m_SampleIndex + 1) % NUM_SAMPLES;
    m_Sum -= m_Samples[m_SampleIndex];
    m_Samples[m_SampleIndex] = m_Elapsed;
    m_Elapsed = 0;
}

PerfMetrics::TimerScope::~TimerScope()
{
    std::scoped_lock lock(s_TimerMutex);

    TimerCollection& timers = GetTimerCollection();

    Timer* timer;

    auto it = timers.find(m_Name);

    if(timers.end() == it)
    {
        return;
    }

    timer = it->second;

    timer->Stop();
}

void
PerfMetrics::BeginFrame()
{
}

void
PerfMetrics::EndFrame()
{
    std::scoped_lock lock(s_TimerMutex);

    TimerCollection& timers = GetTimerCollection();

    for(auto& [name, timer] : timers)
    {
        timer->Sample();
    }
}

void
PerfMetrics::StartTimer(const imstring& name)
{
    std::scoped_lock lock(s_TimerMutex);

    TimerCollection& timers = GetTimerCollection();

    Timer* timer;

    auto it = timers.find(name);

    if(timers.end() == it)
    {
        timer = GetTimerAllocator().New(name);
        if(!timer)
        {
            logError("Failed to start timer '{}'", name);
            return;
        }

        it = timers.emplace(name, timer).first;
    }
    else
    {
        timer = it->second;
    }

    if(!everify(!timer->m_IsRunning,
        "Failed to start timer '{}': Timer is already running", name))
    {
        logError("Failed to start timer '{}': Timer is already running", name);
        return;
    }

    timer->Start();
}

void
PerfMetrics::StopTimer(const imstring& name)
{
    std::scoped_lock lock(s_TimerMutex);

    TimerCollection& timers = GetTimerCollection();

    Timer* timer;

    auto it = timers.find(name);

    if(!everify(timers.end() != it, "Failed to stop timer '{}': No active timer with that name", name))
    {
        logError("Failed to stop timer '{}': No active timer with that name", name);
        return;
    }

    timer = it->second;

    if(!everify(timer->m_IsRunning,
        "Failed to stop timer '{}': Timer is not running", name))
    {
        logError("Failed to stop timer '{}': Timer is not running", name);
        return;
    }

    timer->Stop();
}


unsigned
PerfMetrics::GetTimerCount()
{
    std::scoped_lock lock(s_TimerMutex);

    return static_cast<unsigned>(GetTimerCollection().size());
}

unsigned
PerfMetrics::GetTimers(TimerStat* outStats, const unsigned timerCount)
{
    std::scoped_lock lock(s_TimerMutex);

    TimerCollection& timers = GetTimerCollection();

    unsigned count = 0;

    for(auto& [name, timer] : timers)
    {
        if(count >= timerCount)
        {
            return timerCount;
        }

        outStats[count++] = TimerStat(timer->GetName(), timer->GetValue());
    }

    return count;
}