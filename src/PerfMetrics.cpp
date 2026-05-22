#define __LOGGER_NAME__ "PERF"

#include "PerfMetrics.h"

#include "Log.h"

#include <mutex>

#include <SDL3/SDL.h>

static std::mutex s_Mutex;

inlist<PerfCounter, &PerfCounter::m_ListNode> PerfMetrics::m_Counters;
inlist<PerfTimer, &PerfTimer::m_ListNode> PerfMetrics::m_Timers;

static inline std::uint64_t GetPerfFrequency()
{
    return SDL_GetPerformanceFrequency();
}

static inline std::uint64_t GetPerfTime()
{
    return SDL_GetPerformanceCounter();
}

PerfAggregator::PerfAggregator(const PerfCounter* counter)
    : m_Counter(counter)
{
    m_Stats.m_Name = counter->GetName();
}

void
PerfAggregator::Sample()
{
    constexpr uint64_t SAMPLE_WINDOW = PerfStats::SAMPLE_WINDOW;

    const uint64_t curValue = m_Counter->GetValue();

    m_Stats.m_MinValue = std::min(m_Stats.m_MinValue, curValue);
    m_Stats.m_MaxValue = std::max(m_Stats.m_MaxValue, curValue);

    const uint64_t delta = curValue - m_Stats.m_LastValue;

    const double deltaDouble = static_cast<double>(delta);

    m_Stats.m_LastValue = curValue;

    m_Stats.m_EMA =
        (m_Stats.m_EMA * (SAMPLE_WINDOW - 1) / SAMPLE_WINDOW) + (deltaDouble / SAMPLE_WINDOW);
}

PerfTimerStats::PerfTimerStats(const PerfStats& stats)
{
    m_Name = stats.GetName();
    m_LastValue = static_cast<double>(stats.GetLastValue()) / static_cast<double>(GetPerfFrequency());
    m_MinValue = static_cast<double>(stats.GetMinValue()) / static_cast<double>(GetPerfFrequency());
    m_MaxValue = static_cast<double>(stats.GetMaxValue()) / static_cast<double>(GetPerfFrequency());
    m_EMA = static_cast<double>(stats.GetEMA()) / static_cast<double>(GetPerfFrequency());
}

PerfCounter::PerfCounter(const std::string& name)
    : m_Name(name),
      m_Aggregator(this)
{
    std::lock_guard lock(s_Mutex);
    PerfMetrics::m_Counters.push_back(this);
}

PerfTimer::PerfTimer(const std::string& name)
    : PerfCounter(name)
{
    std::lock_guard lock(s_Mutex);
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

unsigned
PerfMetrics::GetTimerCount()
{
    std::scoped_lock lock(s_Mutex);

    return static_cast<unsigned>(m_Timers.size());
}

unsigned
PerfMetrics::SampleTimers(PerfTimerStats* outStats, const unsigned timerCount)
{
    std::scoped_lock lock(s_Mutex);

    unsigned count = 0;

    for(auto& timer : m_Timers)
    {
        if(count >= timerCount)
        {
            return timerCount;
        }

        timer.m_Aggregator.Sample();

        outStats[count++] = PerfTimerStats(timer.m_Aggregator.GetStats());
    }

    return count;
}

void
PerfMetrics::LogTimers()
{
    for(auto& timer : m_Timers)
    {
        const PerfTimerStats stats(timer.m_Aggregator.GetStats());
        MLG_INFO("{}: {} ms", stats.GetName(), stats.GetLastValue() * 1000.0);
    }
}