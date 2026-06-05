#define MLG_LOGGER_NAME "PERF"

#include "PerfMetrics.h"

#include "Log.h"

#include <mutex>

namespace
{
struct PMGlobals
{
    static inline std::mutex Mutex;
};
} // namespace

inlist<PerfCounter, &PerfCounter::m_ListNode> PerfMetrics::m_Counters;

PerfAggregator::PerfAggregator(const PerfCounter* counter)
    : m_Counter(counter)
{
    m_Stats.m_Name = counter->GetName();
}

void
PerfAggregator::Sample()
{
    const double curValue = m_Counter->GetValue();

    m_Stats.m_MinValue = std::min(m_Stats.m_MinValue, curValue);
    m_Stats.m_MaxValue = std::max(m_Stats.m_MaxValue, curValue);
    m_Stats.m_LastValue = curValue;

    m_Stats.m_EMA = ((m_Stats.m_EMA * (kSampleWindowSize - 1)) + curValue) * invSampleWindowSize;
}

PerfCounter::PerfCounter(std::string name)
    : PerfCounter(std::move(name), PerfCounter::Accumulate, PerfCounterDefaultCategory::Id)
{
}

PerfCounter::PerfCounter(std::string name, const PerfCounterCategoryId categoryId)
    : PerfCounter(std::move(name), PerfCounter::Accumulate, categoryId)
{
}

PerfCounter::PerfCounter(std::string name, const SamplePolicy samplePolicy)
    : PerfCounter(std::move(name), samplePolicy, PerfCounterDefaultCategory::Id)
{
}

PerfCounter::PerfCounter(
    std::string name, const SamplePolicy samplePolicy, const PerfCounterCategoryId categoryId)
    : m_Name(std::move(name)),
      m_SamplePolicy(samplePolicy),
      m_Aggregator(this),
      m_CategoryId(categoryId)
{
    const std::lock_guard lock(PMGlobals::Mutex);
    PerfMetrics::m_Counters.push_back(this);
}

PerfCounter::~PerfCounter()
{
    const std::lock_guard lock(PMGlobals::Mutex);
    PerfMetrics::m_Counters.erase(this);
}

void
PerfCounter::ApplySamplePolicy()
{
    if(m_SamplePolicy == ResetOnSample)
    {
        m_Value.store(0, std::memory_order_relaxed);
    }
}

void
PerfTimer::Start()
{
    m_Timer.Start();
}

void
PerfTimer::Stop()
{
    constexpr float kMsPerSecond = 1000.0f;

    m_Timer.Stop();

    const double elapsed = m_Timer.GetElapsedSeconds();

    m_Counter.Increment(elapsed * kMsPerSecond);
}

size_t
PerfMetrics::GetAllCounterCount()
{
    const std::lock_guard lock(PMGlobals::Mutex);

    return m_Counters.size();
}

size_t
PerfMetrics::SampleAllCounters(std::span<PerfStats>& outStats)
{
    const std::lock_guard lock(PMGlobals::Mutex);

    size_t count = 0;

    for(auto& counter : m_Counters)
    {
        if(count >= outStats.size())
        {
            return outStats.size();
        }

        counter.m_Aggregator.Sample();
        counter.ApplySamplePolicy();

        outStats[count++] = counter.m_Aggregator.GetStats();
    }

    return count;
}

size_t
PerfMetrics::GetCounterCount(const PerfCounterCategoryId categoryId)
{
    const std::lock_guard lock(PMGlobals::Mutex);

    size_t count = 0;
    for(auto& counter : m_Counters)
    {
        if(counter.m_CategoryId == categoryId)
        {
            ++count;
        }
    }

    return count;
}

size_t
PerfMetrics::SampleCounters(const PerfCounterCategoryId categoryId, std::span<PerfStats>& outStats)
{
    const std::lock_guard lock(PMGlobals::Mutex);

    size_t count = 0;

    for(auto& counter : m_Counters)
    {
        if(count >= outStats.size())
        {
            return outStats.size();
        }

        if(counter.m_CategoryId != categoryId)
        {
            continue;
        }

        counter.m_Aggregator.Sample();
        counter.ApplySamplePolicy();

        outStats[count++] = counter.m_Aggregator.GetStats();
    }

    return count;
}

void
PerfMetrics::LogCounters()
{
    const std::lock_guard lock(PMGlobals::Mutex);

    for(auto& counter : m_Counters)
    {
        const PerfStats& stats = counter.m_Aggregator.GetStats();
        if(counter.m_CategoryId != PerfTimerCategory::Id)
        {
            MLG_INFO("{}: {}", stats.GetName(), stats.GetLastValue());
        }
        else
        {
            MLG_INFO("{}: {} ms", stats.GetName(), stats.GetLastValue());
        }
    }
}