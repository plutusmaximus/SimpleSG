#pragma once

#include "inlist.h"
#include "scope_exit.h"

#include <atomic>
#include <span>
#include <string>
#include <string_view>

class PerfMetrics;
class PerfCounter;
class PerfAggregator;

class PerfStats
{
public:

    std::string_view GetName() const { return m_Name; }

    uint64_t GetLastValue() const { return m_LastValue; }
    uint64_t GetMinValue() const { return m_MinValue; }
    uint64_t GetMaxValue() const { return m_MaxValue; }
    double GetEMA() const { return m_EMA; }

private:

    friend PerfAggregator;

    std::string_view m_Name;
    uint64_t m_LastValue{ 0 };
    uint64_t m_MinValue{ std::numeric_limits<uint64_t>::max() };
    uint64_t m_MaxValue{ 0 };
    double m_EMA{ 0 }; // Exponential moving average of counter value, updated on each Sample().
};

class PerfTimerStats
{
public:

    PerfTimerStats() = default;

    explicit PerfTimerStats(const PerfStats& stats);

    std::string_view GetName() const { return m_Name; }

    double GetLastValue() const { return m_LastValue; }
    double GetMinValue() const { return m_MinValue; }
    double GetMaxValue() const { return m_MaxValue; }
    double GetEMA() const { return m_EMA; }

private:

    friend PerfAggregator;

    std::string_view m_Name;
    double m_LastValue{ 0 };
    double m_MinValue{ 0 };
    double m_MaxValue{ 0 };
    double m_EMA{ 0 };
};

class PerfAggregator
{
public:

    static constexpr uint64_t kSampleWindow = 16;
    static constexpr double invSampleWWindow = 1.0 / static_cast<double>(kSampleWindow);

    explicit PerfAggregator(const PerfCounter* counter);

    const PerfStats& GetStats() const { return m_Stats; }

    void Sample();

private:
    const PerfCounter* m_Counter;

    PerfStats m_Stats;
};

class PerfCounter
{
public:
    explicit PerfCounter(std::string name);

    void Increment(const uint64_t count) { m_Value.fetch_add(count, std::memory_order_relaxed); }

    void Decrement(const uint64_t count) { m_Value.fetch_sub(count, std::memory_order_relaxed); }

    void Set(const uint64_t value) { m_Value.store(value, std::memory_order_relaxed); }

    const std::string& GetName() const { return m_Name; }

    uint64_t GetValue() const { return m_Value.load(std::memory_order_relaxed); }

private:

    friend PerfMetrics;

    inlist_node<PerfCounter> m_ListNode;

    std::string m_Name;
    std::atomic<uint64_t> m_Value{ 0 };
    PerfAggregator m_Aggregator;
};

class PerfTimer : public PerfCounter
{
public:

    explicit PerfTimer(std::string name);

    /// @brief  Starts the timer.
    void Start();

    /// @brief  Stops the timer and and adds to the total elapsed time.
    /// Total elapsed time will continue to be accumulated across mutiple Start/Stop calls
    /// until Sample() is called.
    void Stop();

private:

    friend PerfMetrics;

    inlist_node<PerfTimer> m_ListNode;

    uint64_t m_StartTime{ 0 };
};

class PerfMetrics final
{
public:

    PerfMetrics() = delete;
    ~PerfMetrics() = delete;
    PerfMetrics(const PerfMetrics&) = delete;
    PerfMetrics& operator=(const PerfMetrics&) = delete;
    PerfMetrics(PerfMetrics&&) = delete;
    PerfMetrics& operator=(PerfMetrics&&) = delete;

    /// @brief Gets the number of recorded timers.
    static size_t GetTimerCount();

    /// @brief Gets the recorded timers. The caller should provide a buffer of sufficient size based
    /// on GetTimerCount().
    static size_t SampleTimers(std::span<PerfTimerStats>& outStats);

    /// @brief Logs all timers to log output.
    static void LogTimers();

private:

    friend PerfCounter;
    friend PerfTimer;

    static inlist<PerfCounter, &PerfCounter::m_ListNode> m_Counters;
    static inlist<PerfTimer, &PerfTimer::m_ListNode> m_Timers;
};

#define MLG_PERF_TIMER_CONCAT2(a, b) a##b
#define MLG_PERF_TIMER_CONCAT(a, b) MLG_PERF_TIMER_CONCAT2(a, b)
#define MLG_SCOPED_TIMER(name)\
    static PerfTimer MLG_PERF_TIMER_CONCAT(timer, __LINE__)(name);\
    MLG_PERF_TIMER_CONCAT(timer, __LINE__).Start();\
    auto MLG_PERF_TIMER_CONCAT(scope_timer, __LINE__) = scope_exit([&](){MLG_PERF_TIMER_CONCAT(timer, __LINE__).Stop();});