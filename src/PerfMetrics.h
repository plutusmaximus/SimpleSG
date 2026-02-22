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

    /// @brief  Stops the timer and and adds to the total elapsed time.
    /// Total elapsed time will continue to be accumulated across mutiple Start/Stop calls
    /// until Sample() is called.
    void Stop();

    const imstring& GetName() const { return m_Name; }

    /// @brief  Gets the average elapsed time in seconds across the last NUM_SAMPLES runs.
    float GetValue() const;

    unsigned GetCount() const;

private:

    friend class PerfMetrics;

    /// @brief Records the current elapsed time accumulated from a series of Start/Stop calls as a
    /// sample, resets the accumulated time to zero, and updates the running average.
    void Sample();

    static constexpr unsigned NUM_SAMPLES = 16;

    imstring m_Name;
    uint64_t m_StartTime{ 0 };
    uint64_t m_ElapsedSamples[NUM_SAMPLES]{};
    unsigned m_CountSamples[NUM_SAMPLES]{};
    unsigned m_SampleIndex{ 0 };
    uint64_t m_ElapsedSum{ 0 };
    uint64_t m_CountSum{ 0 };
    uint64_t m_Elapsed{ 0 }; // Accumulated elapsed time across Start/Stop calls until Sample() is called.
    unsigned m_Count{ 0 };  // Number of times Start/Stop has been called since the last Sample().
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
        explicit TimerStat(const imstring& name, const float value, const unsigned count)
            : m_Name(name), m_Value(value), m_Count(count)
        {
        }

        const imstring& GetName() const { return m_Name; }

        float GetValue() const { return m_Value; }

        unsigned GetCount() const { return m_Count; }

    private:
        imstring m_Name;
        float m_Value{ 0 };
        unsigned m_Count{ 0 };
    };

    /// @brief  Begins a new frame. This should be called at the beginning of each frame before any
    /// timers are started.  Calling BeginFrame() before the previous frame's EndFrame() is called
    /// will result in undefined behavior.
    static void BeginFrame();

    /// @brief Ends the current frame. This should be called at the end of each frame after all
    /// timers are stopped. Calling EndFrame() before a matching BeginFrame() is called will result
    /// in undefined behavior.
    static void EndFrame();

    /// @brief Gets the number of recorded timers.
    static unsigned GetTimerCount();

    /// @brief Gets the recorded timers. The caller should provide a buffer of sufficient size based
    /// on GetTimerCount().
    static unsigned GetTimers(TimerStat* outStats, const unsigned timerCount);

    /// @brief Logs all timers to log output.
    static void LogTimers();

private:

    PerfMetrics() = delete;
    PerfMetrics(const PerfMetrics&) = delete;
    PerfMetrics& operator=(const PerfMetrics&) = delete;
    PerfMetrics(PerfMetrics&&) = delete;
    PerfMetrics& operator=(PerfMetrics&&) = delete;

    friend class PerfTimer;

    static inlist<PerfTimer, &PerfTimer::m_ListNode> m_Timers;
};