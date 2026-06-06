#pragma once

#include <cstdint>

/// @brief High-precision timer implementation.
class Timer
{
public:
    Timer() = default;
    ~Timer() = default;
    Timer(const Timer&) = default;
    Timer& operator=(const Timer&) = default;
    Timer(Timer&&) = default;
    Timer& operator=(Timer&&) = default;

    void Start();

    void Stop();

    void Clear();

    void Restart()
    {
        Clear();
        Start();
    }

    float GetElapsedSeconds() const;

private:

    uint64_t m_StartTime{0};
    uint64_t m_ElapsedTime{0};
    bool m_Running{false};
};