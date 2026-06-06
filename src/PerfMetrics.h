#pragma once

#include "inlist.h"
#include "scope_exit.h"
#include "Timer.h"

#include <atomic>
#include <span>
#include <string>
#include <string_view>

class PerfMetrics;
class PerfCounter;
class PerfAggregator;

/// @brief Perf counters can have a category Id which can be used to group related counters together.
/// Declare a perf counter category like this:
/// struct MyCategoryTag{};
/// using MyCategory = PerfCounterCategory<MyCategoryTag>;
/// Then perf counters can be created in that category like this:
/// PerfCounter myCounter("MyCounter", MyCategory::Id);
class PerfCounterCategoryId
{
public:

    auto operator<=>(const PerfCounterCategoryId&) const = default;

private:
    explicit PerfCounterCategoryId(const void* uniqueId)
        : m_UniqueId(uniqueId)
    {
    }

    const void* m_UniqueId;

    template<typename T>
    friend class PerfCounterCategory;
};

template<typename Tag>
class PerfCounterCategory
{
private:
    static inline char uniqueCategoryId;
public:

    static inline const PerfCounterCategoryId Id{ &uniqueCategoryId };
};

/// @brief Perf counters that don't have an explicity category are put into
/// the default category.
struct PerfCounterDefaultCategoryTag{};
using PerfCounterDefaultCategory = PerfCounterCategory<PerfCounterDefaultCategoryTag>;

/// @brief Represents aggregated stats for a perf counter, such as min/max/EMA values.
class PerfStats
{
public:

    std::string_view GetName() const { return m_Name; }

    double GetLastValue() const { return m_LastValue; }
    double GetMinValue() const { return m_MinValue; }
    double GetMaxValue() const { return m_MaxValue; }
    double GetEMA() const { return m_EMA; }

private:

    friend PerfAggregator;

    std::string_view m_Name;
    double m_LastValue{ 0 };
    double m_MinValue{ std::numeric_limits<double>::max() };
    double m_MaxValue{ 0 };
    double m_EMA{ 0 }; // Exponential moving average of counter value, updated on each Sample().
};

/// @brief
class PerfAggregator
{
public:

    static constexpr uint64_t kSampleWindowSize = 16;
    static constexpr double invSampleWindowSize = 1.0 / static_cast<double>(kSampleWindowSize);

    explicit PerfAggregator(const PerfCounter* counter);

    const PerfStats& GetStats() const { return m_Stats; }

    void Sample();

private:
    const PerfCounter* m_Counter;

    PerfStats m_Stats;
};

/// @brief Represents a performance counter whose value can be incremented/decremented/set.
/// Periodically call PerfMetrics::SampleCounters() to sample the counter values and update the
/// aggregated stats.
class PerfCounter
{
public:
    enum class SamplePolicy
    {
        Accumulate,    // Counter value will continue to be accumulated.
        ResetOnSample, // Counter value will be reset to 0 on each Sample() call.
    };

    explicit PerfCounter(std::string name);

    PerfCounter(std::string name, const PerfCounterCategoryId categoryId);

    PerfCounter(std::string name, const SamplePolicy samplePolicy);

    PerfCounter(
        std::string name, const SamplePolicy samplePolicy, const PerfCounterCategoryId categoryId);

    ~PerfCounter();

    PerfCounter() = delete;
    PerfCounter(const PerfCounter&) = delete;
    PerfCounter& operator=(const PerfCounter&) = delete;
    PerfCounter(PerfCounter&&) = delete;
    PerfCounter& operator=(PerfCounter&&) = delete;

    void Increment(const double count) { m_Value.fetch_add(count, std::memory_order_relaxed); }

    void Decrement(const double count) { m_Value.fetch_sub(count, std::memory_order_relaxed); }

    void Set(const double value) { m_Value.store(value, std::memory_order_relaxed); }

    const std::string& GetName() const { return m_Name; }

    double GetValue() const { return m_Value.load(std::memory_order_relaxed); }

private:

    void ApplySamplePolicy();

    friend PerfMetrics;
    template<typename T> friend class PerfCounterCategory;

    inlist_node<PerfCounter> m_ListNode;

    std::string m_Name;
    std::atomic<double> m_Value{ 0 };
    PerfAggregator m_Aggregator;
    SamplePolicy m_SamplePolicy{ SamplePolicy::Accumulate };
    PerfCounterCategoryId m_CategoryId{ PerfCounterDefaultCategory::Id };
};

/// @brief  Helper class to measure elapsed time update a PerfCounter with the result.
class PerfTimer
{
public:

    explicit PerfTimer(PerfCounter& counter)
        : m_Counter(&counter)
    {
    }

    /// @brief  Starts the timer.
    void Start();

    /// @brief  Stops the timer and and adds to the total elapsed time.
    /// Total elapsed time will continue to be accumulated across mutiple Start/Stop calls
    /// until Sample() is called.
    void Stop();

private:

    PerfCounter* m_Counter;
    Timer m_Timer;
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

    /// @brief Gets the number of recorded counters.
    static size_t GetAllCounterCount();

    template<typename Cat = PerfCounterDefaultCategory>
    static size_t GetCounterCount()
    {
        return GetCounterCount(Cat::Id);
    }

    template<typename Cat = PerfCounterDefaultCategory>
    static size_t SampleCounters(std::span<PerfStats>& outStats)
    {
        return SampleCounters(Cat::Id, outStats);
    }

    /// @brief Gets the aggregated counter stats. The caller should provide a buffer of sufficient size based
    /// on GetCounterCount().
    static size_t SampleAllCounters(std::span<PerfStats>& outStats);

    /// @brief Logs all counters to log output.
    static void LogCounters();

private:

    static size_t GetCounterCount(const PerfCounterCategoryId categoryId);
    static size_t SampleCounters(const PerfCounterCategoryId categoryId, std::span<PerfStats>& outStats);

    friend PerfCounter;

    static inlist<PerfCounter, &PerfCounter::m_ListNode> m_Counters;
};

struct PerfTimerCategoryTag{};
using PerfTimerCategory = PerfCounterCategory<PerfTimerCategoryTag>;

#define MLG_PERF_TIMER_CONCAT2(a, b) a##b
#define MLG_PERF_TIMER_CONCAT(a, b) MLG_PERF_TIMER_CONCAT2(a, b)

/// @brief Helper macro to time a scope and record the result in a PerfCounter. Usage:
/// {   // Scope for the timer
///     MLG_SCOPED_TIMER("MyCounter");
///     // Code to be timed goes here.
/// }
#define MLG_SCOPED_TIMER(name)\
    static PerfCounter MLG_PERF_TIMER_CONCAT(counter, __LINE__)(name, PerfCounter::SamplePolicy::ResetOnSample, PerfTimerCategory::Id);\
    PerfTimer MLG_PERF_TIMER_CONCAT(timer, __LINE__)(MLG_PERF_TIMER_CONCAT(counter, __LINE__));\
    MLG_PERF_TIMER_CONCAT(timer, __LINE__).Start();\
    auto MLG_PERF_TIMER_CONCAT(scope_timer, __LINE__) = scope_exit([&](){MLG_PERF_TIMER_CONCAT(timer, __LINE__).Stop();});