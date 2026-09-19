#include "CoopTask.h"

ICoopTask::~ICoopTask()
{
    MLG_ASSERT(Stage::Pending != m_Stage, "Destroying pending task");
}

Result<>
ICoopTask::Start()
{
    MLG_CHECKV(m_Stage == Stage::None, "Task has already been started.");

    m_Stage = Stage::Pending;

    Result<> result = OnStart();
    if (!result)
    {
        m_Stage = Stage::Complete;
    }
    return result;
}

/// Returns true if the task is still pending, false if it is complete.
bool
ICoopTask::IsPending() const
{
    if(!MLG_VERIFY(m_Stage != Stage::None, "Task is not started"))
    {
        return false;
    }

    return m_Stage == Stage::Pending;
}

void
ICoopTask::Update()
{
    if(MLG_VERIFY(IsPending(), "Task is not running"))
    {
        OnUpdate();
    }
}

void
ICoopTask::SetComplete()
{
    if(MLG_VERIFY(m_Stage == Stage::Pending, "Task is not pending."))
    {
        m_Stage = Stage::Complete;
    }
}

CoopTaskBatch::CoopTaskBatch(std::initializer_list<ICoopTask*> tasks)
    : CoopTaskBatch(std::vector<ICoopTask*>(tasks))
{
}

CoopTaskBatch::CoopTaskBatch(std::span<ICoopTask*> tasks)
    : CoopTaskBatch(std::vector<ICoopTask*>(tasks.begin(), tasks.end()))
{
}

CoopTaskBatch::CoopTaskBatch(std::vector<ICoopTask*> tasks)
    : m_Tasks(std::move(tasks))
{
}

// private:

Result<>
CoopTaskBatch::OnStart()
{
    MLG_CHECKV(Stage::None == m_Stage, "Task is already in progress");

    MLG_CHECKV(!m_Tasks.empty(), "No tasks provided");

    for(size_t i = 0; i < m_Tasks.size(); )
    {
        MLG_ASSERT(m_Tasks[i] != nullptr, "Task is null");

        if(m_Tasks[i]->Start())
        {
            ++i;
        }
        else
        {
            m_Tasks[i] = m_Tasks.back();
            m_Tasks.pop_back();
        }
    }

    m_Stage = Stage::Pending;

    return Result<>::Ok;
}

void
CoopTaskBatch::OnUpdate()
{
    for(size_t i = 0; i < m_Tasks.size(); )
    {
        ICoopTask* task = m_Tasks[i];
        if(task->IsPending())
        {
            task->Update();
            ++i;
        }
        else
        {
            m_Tasks[i] = m_Tasks.back();
            m_Tasks.pop_back();
        }
    }

    if(m_Tasks.empty())
    {
        m_Stage = Stage::Succeeded;
        SetComplete();
    }
}