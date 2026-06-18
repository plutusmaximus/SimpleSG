#pragma once

#include <cstdint>

/// @brief High-precision timer implementation.
class Timer
{
public:

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