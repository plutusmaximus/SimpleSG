#pragma once

#include <cstdint>

/// @brief High-precision timer implementation.
class Stopwatch
{
public:
    Stopwatch();

    /// @brief Marks the current time and returns the elapsed time in seconds since the last mark.
    float Mark();

    /// @brief Returns the elapsed time in seconds since the last mark without updating the last time.
    float Elapsed() const;

private:

    std::int64_t m_LastTime;
};