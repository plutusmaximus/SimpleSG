#define MLG_LOGGER_NAME "PERF"

#include "PerfMetrics.h"

#include "Log.h"

#include <mutex>

#include <SDL3/SDL.h>

namespace
{
struct PMGlobals
{
    static inline std::mutex Mutex;
    static inline const uint64_t PerfFrequency = SDL_GetPerformanceFrequency();
    static inline const double InvPerfFrequency = 1.0 / static_cast<double>(PerfFrequency);
};

inline std::uint64_t GetPerfTime()
{
    return SDL_GetPerformanceCounter();
}
} // namespace

inlist<PerfCounter, &PerfCounter::m_ListNode> PerfMetrics::m_Counters;
inlist<PerfTimer, &PerfTimer::m_ListNode> PerfMetrics::m_Timers;

PerfAggregator::PerfAggregator(const PerfCounter* counter)
    : m_Counter(counter)
{
    m_Stats.m_Name = counter->GetName();
}

void
PerfAggregator::Sample()
{
    const uint64_t curValue = m_Counter->GetValue();

    m_Stats.m_MinValue = std::min(m_Stats.m_MinValue, curValue);
    m_Stats.m_MaxValue = std::max(m_Stats.m_MaxValue, curValue);

    const uint64_t delta = curValue - m_Stats.m_LastValue;

    m_Stats.m_LastValue = curValue;

    const double deltaDouble = static_cast<double>(delta);

    m_Stats.m_EMA = ((m_Stats.m_EMA * (kSampleWindow - 1)) + deltaDouble) * invSampleWWindow;
}

PerfTimerStats::PerfTimerStats(const PerfStats& stats)
    : m_Name(stats.GetName()),
      m_LastValue(
          static_cast<double>(stats.GetLastValue()) * PMGlobals::InvPerfFrequency),
      m_MinValue(
          static_cast<double>(stats.GetMinValue()) * PMGlobals::InvPerfFrequency),
      m_MaxValue(
          static_cast<double>(stats.GetMaxValue()) * PMGlobals::InvPerfFrequency),
      m_EMA(stats.GetEMA() * PMGlobals::InvPerfFrequency)
{
}

PerfCounter::PerfCounter(std::string name)
    : m_Name(std::move(name)),
      m_Aggregator(this)
{
    const std::lock_guard lock(PMGlobals::Mutex);
    PerfMetrics::m_Counters.push_back(this);
}

PerfTimer::PerfTimer(std::string name)
    : PerfCounter(std::move(name))
{
    const std::lock_guard lock(PMGlobals::Mutex);
    PerfMetrics::m_Timers.push_back(this);
}

void
PerfTimer::Start()
{
    m_StartTime = GetPerfTime();
}

void
PerfTimer::Stop()
{
    const uint64_t elapsed = GetPerfTime() - m_StartTime;

    Increment(elapsed);
}

size_t
PerfMetrics::GetTimerCount()
{
    const std::lock_guard lock(PMGlobals::Mutex);

    return m_Timers.size();
}

size_t
PerfMetrics::SampleTimers(std::span<PerfTimerStats>& outStats)
{
    const std::lock_guard lock(PMGlobals::Mutex);

    size_t count = 0;

    for(auto& timer : m_Timers)
    {
        if(count >= outStats.size())
        {
            return outStats.size();
        }

        timer.m_Aggregator.Sample();

        outStats[count++] = PerfTimerStats(timer.m_Aggregator.GetStats());
    }

    return count;
}

void
PerfMetrics::LogTimers()
{
    const std::lock_guard lock(PMGlobals::Mutex);

    for(auto& timer : m_Timers)
    {
        constexpr float kMsPerSecond = 1000.0f;
        const PerfTimerStats stats(timer.m_Aggregator.GetStats());
        MLG_INFO("{}: {} ms", stats.GetName(), stats.GetLastValue() * kMsPerSecond);
    }
}