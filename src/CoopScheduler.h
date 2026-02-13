#pragma once

#include "Error.h"
#include "inlist.h"
#include "instack.h"

/// @brief Cooperative task scheduler.
///
/// The scheduler manages lightweight tasks (`CoopTask`) that cooperatively advance
/// over time via `Start()` and `Update()` calls. Tasks can be grouped with
/// `CoopTaskGroup` so related work is tracked together; a group is pending while
/// any of its tasks are pending. Typical usage is:
/// 1) Create tasks (often heap-allocated).
/// 2) Optionally push a group, enqueue tasks, then pop the group.
/// 3) Call `ProcessPendingTasks()` each frame/tick until `HasPendingTasks()` is false.
///
/// When a task is complete (IsComplete() returns true ) it is automatically removed from its group
/// and the pending list. The scheduler will call `Dispose()` on completed tasks, which is
/// responsible for deleting the task object.
///
/// Dispose() can delete the task, return it to an object pool, or do any other cleanup as needed.
/// The key point is that the task is responsible for managing its own lifetime.
///
/// Example:
/// @code
/// struct MyTask final : CoopTask {
///     bool started = false;
///     void Start() override { started = true; }
///     void Update() override { /* do work */ }
///     bool IsStarted() const override { return started; }
///     bool IsPending() const override { return !IsComplete(); }
///     bool IsComplete() const override { return /* done */ true; }
///     void Dispose() override { delete this; }
/// };
///
/// CoopScheduler scheduler;
/// auto task1 = new MyTask1();
/// scheduler.Enqueue(task1);
///
/// while (task1->IsPending()) {
///     scheduler.ProcessPendingTasks();
/// }
/// // Task1 is complete and has been deleted at this point.
///
/// auto task2 = new MyTask2();
/// auto task3 = new MyTask3();
/// CoopTaskGroup group;
/// scheduler.PushGroup(&group);
/// scheduler.Enqueue(task2);
/// scheduler.Enqueue(task3);
/// scheduler.PopGroup(&group);
///
/// while (group.IsPending()) {
///     scheduler.ProcessPendingTasks();
/// }
/// // Task2 and Task3 are complete and have been deleted at this point.
/// @endcode

class CoopTaskGroup;
class CoopScheduler;

/// @brief Base class for asynchronous operations.
class CoopTask
{
    friend class CoopTaskGroup;
    friend class CoopScheduler;

public:
    virtual ~CoopTask()
    {
        eassert(!m_PendingTaskNode.IsLinked(), "CoopTask destroyed while still pending");
        eassert(!m_GroupNode.IsLinked(), "CoopTask destroyed while still part of a group");
        eassert(!m_Group, "CoopTask destroyed while still part of a group");
    }

    virtual void Start() = 0;

    virtual void Update() = 0;

    virtual bool IsStarted() const = 0;
    virtual bool IsPending() const = 0;
    virtual bool IsComplete() const = 0;

    void RemoveFromGroup();

protected:
    CoopTask() = default;

    virtual void Dispose() = 0;

private:
    inlist_node<CoopTask> m_PendingTaskNode;

    inlist_node<CoopTask> m_GroupNode;

    /// Group that this task is part of.
    CoopTaskGroup* m_Group{ nullptr };
};

/// @brief A group of asynchronous operations that are related and should be processed together.
/// As long as any operation in the group is pending, the group is considered pending.
class CoopTaskGroup
{
    friend class CoopScheduler;

public:
    ~CoopTaskGroup() { eassert(!IsPending(), "CoopTaskGroup destroyed while tasks still pending"); }

    bool IsPending() const { return !m_Operations.empty(); }

    void Add(CoopTask* task);

    void Remove(CoopTask* task);

private:
    inlist<CoopTask, &CoopTask::m_GroupNode> m_Operations;

    instack_node<CoopTaskGroup> m_GroupNode;
};

class CoopScheduler
{
public:
    void Enqueue(CoopTask* task);

    void PushGroup(CoopTaskGroup* group);

    void PopGroup(CoopTaskGroup* group);

    void ProcessPendingTasks();

    bool HasPendingTasks() const { return !m_PendingTasks.empty(); }

private:
    inlist<CoopTask, &CoopTask::m_PendingTaskNode> m_PendingTasks;

    instack<CoopTaskGroup, &CoopTaskGroup::m_GroupNode> m_TaskGroups;
};