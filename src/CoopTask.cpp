#include "CoopTask.h"

ICoopTask2::~ICoopTask2()
{
    MLG_ASSERT(Stage::Pending != m_Stage, "Destroying pending task");
}

Result<>
ICoopTask2::Start()
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
ICoopTask2::IsPending() const
{
    if(!MLG_VERIFY(m_Stage != Stage::None, "Task is not started"))
    {
        return false;
    }

    return m_Stage == Stage::Pending;
}

void
ICoopTask2::Update()
{
    if(MLG_VERIFY(IsPending(), "Task is not running"))
    {
        OnUpdate();
    }
}

void
ICoopTask2::SetComplete()
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

CoopTaskBatch::~CoopTaskBatch()
{
    MLG_ASSERT(Stage::None == m_Stage || !CoopTaskBatch::IsPending(), "Destroying pending task");
}

Result<>
CoopTaskBatch::Begin()
{
    MLG_CHECKV(Stage::None == m_Stage, "Task is already in progress");

    MLG_CHECKV(!m_Tasks.empty(), "No tasks provided");

    for(size_t i = 0; i < m_Tasks.size(); )
    {
        MLG_ASSERT(m_Tasks[i] != nullptr, "Task is null");

        if(m_Tasks[i]->Begin())
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

bool
CoopTaskBatch::IsPending() const
{
    MLG_ASSERT(Stage::None != m_Stage, "Task is not started");

    return Stage::Pending == m_Stage;
}

void
CoopTaskBatch::Update()
{
    if(!MLG_VERIFY(IsPending(), "Task is not running"))
    {
        return;
    }

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
    }
}