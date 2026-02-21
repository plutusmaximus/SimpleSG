#pragma once

#include "imstring.h"
#include "inlist.h"

class PerfMetrics;

class PerfTimer
{
public:

    class TimerScope
    {
    public:

        ~TimerScope()
        {
            m_Timer.Stop();
        }

    private:
        friend class PerfTimer;

        explicit TimerScope(PerfTimer& timer)
            : m_Timer(timer)
        {
            timer.Start();
        }

        PerfTimer& m_Timer;
    };

    explicit PerfTimer(const imstring& name);

    /// @brief  Starts the timer.
    void Start();

    /// @brief  Starts a scoped timer. The timer will be automatically stopped when the returned
    /// TimerScope object goes out of scope.
    TimerScope StartScoped() { return TimerScope(*this); }

    /// @brief  Stops the timer and computes a running average of the elapsed time across the
    /// last NUM_SAMPLES runs.
    void Stop();

    const imstring& GetName() const { return m_Name; }

    /// @brief  Gets the average elapsed time in seconds across the last NUM_SAMPLES runs.
    float GetValue() const;

private:

    friend class PerfMetrics;

    static constexpr unsigned NUM_SAMPLES = 16;

    imstring m_Name;
    uint64_t m_StartTime{ 0 };
    uint64_t m_Samples[NUM_SAMPLES]{};
    unsigned m_SampleIndex{ 0 };
    uint64_t m_Sum{ 0 };
    bool m_IsRunning{ false };

    inlist_node<PerfTimer> m_ListNode;
};

class PerfMetrics final
{
public:

    class TimerStat
    {
    public:

        TimerStat() = default;
        explicit TimerStat(const imstring& name, const float value)
            : m_Name(name), m_Value(value)
        {
        }

        const imstring& GetName() const { return m_Name; }

        float GetValue() const { return m_Value; }

    private:
        imstring m_Name;
        float m_Value{ 0 };
    };

    /// @brief Gets the number of recorded timers.
    static unsigned GetTimerCount();

    /// @brief Gets the recorded timers. The caller should provide a buffer of sufficient size based
    /// on GetTimerCount().
    static unsigned GetTimers(TimerStat* outStats, const unsigned timerCount);

private:

    PerfMetrics() = delete;
    PerfMetrics(const PerfMetrics&) = delete;
    PerfMetrics& operator=(const PerfMetrics&) = delete;
    PerfMetrics(PerfMetrics&&) = delete;
    PerfMetrics& operator=(PerfMetrics&&) = delete;

    friend class PerfTimer;

    static inlist<PerfTimer, &PerfTimer::m_ListNode> m_Timers;
};