#include <gtest/gtest.h>

#include "CoopScheduler.h"

namespace
{
    struct TaskState
    {
        int startCount = 0;
        int updateCount = 0;
        int disposeCount = 0;
        int completeAfter = 1;
        bool started = false;
        bool completed = false;
    };

    struct TestTask final : CoopTask
    {
        explicit TestTask(TaskState* state)
            : m_State(state)
        {
        }

        void Start() override
        {
            m_State->started = true;
            ++m_State->startCount;
        }

        void Update() override
        {
            ++m_State->updateCount;
            if (m_State->updateCount >= m_State->completeAfter)
            {
                m_State->completed = true;
            }
        }

        bool IsStarted() const override { return m_State->started; }
        bool IsPending() const override { return !IsComplete(); }
        bool IsComplete() const override { return m_State->completed; }

    protected:
        void Dispose() override
        {
            ++m_State->disposeCount;
            delete this;
        }

    private:
        TaskState* m_State = nullptr;
    };

    TEST(CoopScheduler, Enqueue_StartsAndProcessesTask)
    {
        CoopScheduler scheduler;
        TaskState state;
        state.completeAfter = 2;

        auto* task = new TestTask(&state);
        scheduler.Enqueue(task);

        EXPECT_TRUE(state.started);
        EXPECT_EQ(state.startCount, 1);
        EXPECT_TRUE(scheduler.HasPendingTasks());

        scheduler.ProcessPendingTasks();
        EXPECT_EQ(state.updateCount, 1);
        EXPECT_EQ(state.disposeCount, 0);
        EXPECT_TRUE(scheduler.HasPendingTasks());

        scheduler.ProcessPendingTasks();
        EXPECT_EQ(state.updateCount, 2);
        EXPECT_EQ(state.disposeCount, 1);
        EXPECT_FALSE(scheduler.HasPendingTasks());
    }

    TEST(CoopScheduler, TaskGroup_PendingUntilAllComplete)
    {
        CoopScheduler scheduler;
        CoopTaskGroup group;

        TaskState state1;
        state1.completeAfter = 1;
        TaskState state2;
        state2.completeAfter = 2;

        scheduler.PushGroup(&group);
        scheduler.Enqueue(new TestTask(&state1));
        scheduler.Enqueue(new TestTask(&state2));
        scheduler.PopGroup(&group);

        EXPECT_TRUE(group.IsPending());
        EXPECT_TRUE(scheduler.HasPendingTasks());

        scheduler.ProcessPendingTasks();
        EXPECT_EQ(state1.disposeCount, 1);
        EXPECT_EQ(state2.disposeCount, 0);
        EXPECT_TRUE(group.IsPending());

        scheduler.ProcessPendingTasks();
        EXPECT_EQ(state2.disposeCount, 1);
        EXPECT_FALSE(group.IsPending());
        EXPECT_FALSE(scheduler.HasPendingTasks());
    }

    TEST(CoopScheduler, GroupTracksOnlyGroupedTasks)
    {
        CoopScheduler scheduler;
        CoopTaskGroup group;

        TaskState groupedState;
        groupedState.completeAfter = 1;
        TaskState ungroupedState;
        ungroupedState.completeAfter = 1;

        scheduler.PushGroup(&group);
        scheduler.Enqueue(new TestTask(&groupedState));
        scheduler.PopGroup(&group);
        scheduler.Enqueue(new TestTask(&ungroupedState));

        EXPECT_TRUE(group.IsPending());

        scheduler.ProcessPendingTasks();
        EXPECT_EQ(groupedState.disposeCount, 1);
        EXPECT_EQ(ungroupedState.disposeCount, 1);
        EXPECT_FALSE(group.IsPending());
        EXPECT_FALSE(scheduler.HasPendingTasks());
    }
}
