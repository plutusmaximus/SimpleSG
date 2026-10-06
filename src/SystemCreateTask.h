#pragma once

#include "System.h"

#include "CoopTask.h"
#include "GpuHelper.h"
#include "Result.h"

#include <memory>
#include <string_view>

/// Task for creating a System instance asynchronously.
class System::CreateTask : public ICoopTask<std::string_view>
{
public:
    CreateTask();
    ~CreateTask() override;
    CreateTask(const CreateTask&) = delete;
    CreateTask& operator=(const CreateTask&) = delete;
    CreateTask(CreateTask&&) = delete;
    CreateTask& operator=(CreateTask&&) = delete;

    /// Returns the System instance if the task succeeded, otherwise returns an error.
    /// This method will invalidate the task, so it can only be called once.
    Result<std::unique_ptr<System>> Take();

private:
    friend System;

    enum class Stage
    {
        None,
        CreatingGpuHelper,
        Succeeded,
        Failed
    };

    Result<> OnStart(const std::string_view appName) override;

    void OnUpdate() override;

    Stage m_Stage{ Stage::None };

    GpuHelper::CreateTask m_GpuHelperTask;

    std::unique_ptr<Impl> m_Impl;
};