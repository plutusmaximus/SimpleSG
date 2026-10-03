#define MLG_LOGGER_NAME "PERF"

#include "PerfMetrics.h"

#include "Log.h"
#include "SanitizerHelpers.h"

#include <mutex>

namespace
{

struct PerfMetricsState
{
    std::mutex Mutex;
};

PerfMetricsState&
GetPerfMetricsState()
{
    static PerfMetricsState* state = new PerfMetricsState; // NOLINT(cppcoreguidelines-owning-memory)

    // We intentionally leak this, so hide it from leak sanitizers
    MLG_LSAN_IGNORE_OBJECT(state);

    return *state;
}
} // namespace

////////// PerfStats

std::string_view
PerfStats::GetName() const
{
    return m_Counter->GetName();
}

PerfCounterCategoryId
PerfStats::GetCategoryId() const
{
    return m_Counter->GetCategoryId();
}


////////// PerfAggregator

PerfAggregator::PerfAggregator(const PerfCounter& counter)
    : m_Counter(&counter),
      m_Stats(counter)
{
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

////////// PerfCounter

PerfCounter::PerfCounter(const PerfCounterParams& params)
    : m_Name(params.Name),
      m_Aggregator(*this),
      m_SamplePolicy(params.Policy),
      m_CategoryId(params.CategoryId)
{
    const std::lock_guard lock(GetPerfMetricsState().Mutex);
    PerfMetrics::GetCounters().push_back(this);
}

PerfCounter::~PerfCounter()
{
    const std::lock_guard lock(GetPerfMetricsState().Mutex);
    PerfMetrics::GetCounters().erase(this);
}

void
PerfCounter::ApplySamplePolicy()
{
    if(m_SamplePolicy == SamplePolicy::ResetOnSample)
    {
        m_Value.store(0, std::memory_order_relaxed);
    }
}

////////// PerfTimer

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

    m_Counter->Increment(elapsed * kMsPerSecond);
}

////////// PerfMetrics

size_t
PerfMetrics::GetAllCounterCount()
{
    const std::lock_guard lock(GetPerfMetricsState().Mutex);

    return GetCounters().size();
}

size_t
PerfMetrics::SampleAllCounters(std::span<const PerfStats*>& outStats)
{
    const std::lock_guard lock(GetPerfMetricsState().Mutex);

    size_t count = 0;

    for(auto& counter : GetCounters())
    {
        if(count >= outStats.size())
        {
            return outStats.size();
        }

        counter.m_Aggregator.Sample();
        counter.ApplySamplePolicy();

        outStats[count++] = &counter.m_Aggregator.GetStats();
    }

    return count;
}

void
PerfMetrics::LogCounters()
{
    const std::lock_guard lock(GetPerfMetricsState().Mutex);

    for(auto& counter : GetCounters())
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

// private:

PerfMetrics::CounterList&
PerfMetrics::GetCounters()
{
    static CounterList* counters = new CounterList; // NOLINT(cppcoreguidelines-owning-memory)

    // We intentionally leak this, so hide it from leak sanitizers
    MLG_LSAN_IGNORE_OBJECT(counters);

    return *counters;
}

size_t
PerfMetrics::GetCounterCount(const PerfCounterCategoryId categoryId)
{
    const std::lock_guard lock(GetPerfMetricsState().Mutex);

    size_t count = 0;
    for(auto& counter : GetCounters())
    {
        if(counter.m_CategoryId == categoryId)
        {
            ++count;
        }
    }

    return count;
}

size_t
PerfMetrics::SampleCounters(const PerfCounterCategoryId categoryId, std::span<const PerfStats*>& outStats)
{
    const std::lock_guard lock(GetPerfMetricsState().Mutex);

    size_t count = 0;

    for(auto& counter : GetCounters())
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

        outStats[count++] = &counter.m_Aggregator.GetStats();
    }

    return count;
}