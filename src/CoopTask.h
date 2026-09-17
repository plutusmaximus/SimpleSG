#pragma once

#include "Result.h"

/// A cooperative task that executes over multiple frames.
/// Each frame check IsPending() to see if the task is still running,
/// and call Update() to advance the task.
class ICoopTask // NOLINT(cppcoreguidelines-special-member-functions)
{
public:
    virtual ~ICoopTask() = default;

    /// Begins the task. Returns a Result indicating success or failure.
    virtual Result<> Begin() = 0;

    /// Returns true if the task is still pending, false if it is complete.
    virtual bool IsPending() const = 0;

    /// Updates the task, advancing it to the next stage.
    /// This must be called periodically while IsPending() returns true.
    virtual void Update() = 0;
};