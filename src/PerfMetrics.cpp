#define MLG_LOGGER_NAME "PERF"

#include "PerfMetrics.h"

#include "Log.h"
#include "SanitizerHelpers.h"

#include <mutex>

namespace
{
std::mutex& GetMutex()
{
    static std::mutex* mutex = new std::mutex; // NOLINT(cppcoreguidelines-owning-memory)

    // We intentionally leak this, so hide it from leak sanitizers
    MLG_LSAN_IGNORE_OBJECT(mutex);
    
    return *mutex;
}

StringArena& GetStringArena()
{
    constexpr size_t kStringArenaChunkSize = 1024uz * 4uz;
    static StringArena* arena = new StringArena(kStringArenaChunkSize); // NOLINT(cppcoreguidelines-owning-memory)

    // We intentionally leak this, so hide it from leak sanitizers
    MLG_LSAN_IGNORE_OBJECT(arena);

    return *arena;
}

StringHandle ValidateName(const std::string_view& name)
{
    MLG_ASSERT(!name.empty(), "Empty perf counter name");

    return GetStringArena().NewString(name);
}
} // namespace

inlist<PerfCounter, &PerfCounter::m_ListNode> PerfMetrics::m_Counters;

PerfAggregator::PerfAggregator(const PerfCounter* counter)
    : m_Counter(counter),
    m_Stats(counter->GetName())
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

PerfCounter::PerfCounter(const PerfCounterParams& params)
    : m_Name(ValidateName(params.Name)),
      m_Aggregator(this),
      m_SamplePolicy(params.Policy),
      m_CategoryId(params.CategoryId)
{
    const std::lock_guard lock(GetMutex());
    PerfMetrics::m_Counters.push_back(this);
}

PerfCounter::~PerfCounter()
{
    const std::lock_guard lock(GetMutex());
    PerfMetrics::m_Counters.erase(this);
}

void
PerfCounter::ApplySamplePolicy()
{
    if(m_SamplePolicy == SamplePolicy::ResetOnSample)
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

    m_Counter->Increment(elapsed * kMsPerSecond);
}

size_t
PerfMetrics::GetAllCounterCount()
{
    const std::lock_guard lock(GetMutex());

    return m_Counters.size();
}

size_t
PerfMetrics::SampleAllCounters(std::span<PerfStats>& outStats)
{
    const std::lock_guard lock(GetMutex());

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
    const std::lock_guard lock(GetMutex());

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
    const std::lock_guard lock(GetMutex());

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
    const std::lock_guard lock(GetMutex());

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