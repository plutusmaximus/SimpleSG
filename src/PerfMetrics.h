#pragma once

#include "imstring.h"

class PerfMetrics final
{
public:

    class Timer
    {
    public:

        Timer() = default;
        explicit Timer(const imstring& name);

        const imstring& GetName() const { return m_Name; }

        float GetValue() const;

    private:
        friend class PerfMetrics;

        void Start();

        void Stop();

        void Sample();

        static constexpr unsigned NUM_SAMPLES = 16;

        imstring m_Name;
        uint64_t m_StartTime{0};
        uint64_t m_Samples[NUM_SAMPLES]{};
        unsigned m_SampleIndex{ 0 };
        uint64_t m_Sum{0};
        uint64_t m_Elapsed{0};
        bool m_IsRunning{false};
    };

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

    class TimerScope
    {
    public:

        ~TimerScope();

    private:
        friend class PerfMetrics;

        explicit TimerScope(const imstring& name)
            : m_Name(name)
        {
            PerfMetrics::StartTimer(m_Name);
        }

        imstring m_Name;
    };

    static void BeginFrame();

    static void EndFrame();

    /// @brief Starts a timer with the given name.
    static void StartTimer(const imstring& name);

    /// @brief Starts a scoped timer with the given name. The timer will be automatically stopped
    /// when the returned TimerScope object goes out of scope.
    static TimerScope StartScopedTimer(const imstring& name)
    {
        return TimerScope(name);
    }

    /// @brief Stops the timer with the given name and records the elapsed time.
    /// Timers can be started and stopped multiple times, and the elapsed time will be accumulated
    /// across all runs. StopTimer() must be called before calling StartTimer() again for the same
    /// timer name, and vice versa.  If StartTimer() is called while the timer is already running,
    /// or StopTimer() is called while the timer is not running, an error will be logged and the
    /// call will be ignored.
    static void StopTimer(const imstring& name);

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
};