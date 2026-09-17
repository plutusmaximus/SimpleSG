#pragma once

#include "Result.h"

#include <initializer_list>
#include <span>
#include <vector>

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

/// A batch of cooperative tasks that completes when all tasks in the batch have completed.
/// The batch itself is a cooperative task and can be used like any other ICoopTask.
/// Note: The tasks in the batch must be non-null and remain valid for the lifetime of the batch.
class CoopTaskBatch : public ICoopTask
{
public:
    CoopTaskBatch(std::initializer_list<ICoopTask*> tasks);
    explicit CoopTaskBatch(std::span<ICoopTask*> tasks);
    explicit CoopTaskBatch(std::vector<ICoopTask*> tasks);
    ~CoopTaskBatch() override;
    CoopTaskBatch() = default;
    CoopTaskBatch(const CoopTaskBatch&) = delete;
    CoopTaskBatch& operator=(const CoopTaskBatch&) = delete;
    CoopTaskBatch(CoopTaskBatch&&) = delete;
    CoopTaskBatch& operator=(CoopTaskBatch&&) = delete;

    Result<> Begin() override;

    bool IsPending() const override;

    void Update() override;

private:
    enum class Stage
    {
        None,
        Pending,
        Succeeded,
        Failed
    };
    Stage m_Stage{ Stage::None };
    std::vector<ICoopTask*> m_Tasks;
};