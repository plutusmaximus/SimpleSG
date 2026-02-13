#include "CoopScheduler.h"

void
CoopTask::RemoveFromGroup()
{
    if(!m_Group)
    {
        return;
    }

    m_Group->Remove(this);
}

void
CoopTaskGroup::Add(CoopTask* task)
{
    eassert(task->m_Group == nullptr, "Invalid state: task already part of a group");
    m_Operations.push_back(task);
    task->m_Group = this;
}

void
CoopTaskGroup::Remove(CoopTask* task)
{
    eassert(task->m_Group == this, "Invalid state: task not part of this group");
    eassert(!m_Operations.empty(), "Invalid state: group is empty");

    m_Operations.erase(task);
    task->m_Group = nullptr;
}

void
CoopScheduler::Enqueue(CoopTask* task)
{
    if(!m_TaskGroups.empty())
    {
        m_TaskGroups.top()->Add(task);
    }

    m_PendingTasks.push_back(task);

    task->Start();
}

void
CoopScheduler::PushGroup(CoopTaskGroup* group)
{
    eassert(!group->IsPending(), "Cannot push group with pending operations");

    m_TaskGroups.push(group);
}

void
CoopScheduler::PopGroup(CoopTaskGroup* group)
{
    eassert(m_TaskGroups.top() == group, "Invalid state: group not at top of stack");

    m_TaskGroups.pop();
}

void
CoopScheduler::ProcessPendingTasks()
{
    auto it = m_PendingTasks.begin();

    while(it != m_PendingTasks.end())
    {
        auto next = it;
        ++next;

        it->Update();

        if(it->IsComplete())
        {
            m_PendingTasks.erase(it);
            it->RemoveFromGroup();
            it->Dispose();
        }
        it = next;
    }
}