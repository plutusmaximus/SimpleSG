#pragma once

#include "Result.h"

#include <cstdint>
#include <string>
#include <vector>
#include <webgpu/webgpu_cpp.h>

class GpuHelper;
class FileFetcher;

/// A task that fetches a shader from disk and creates a wgpu::ShaderModule.
class ShaderFetcher
{
public:

    ShaderFetcher(std::string path, const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    ShaderFetcher() = delete;
    ~ShaderFetcher();
    ShaderFetcher(const ShaderFetcher&) = delete;
    ShaderFetcher& operator=(const ShaderFetcher&) = delete;
    ShaderFetcher(ShaderFetcher&&) = delete;
    ShaderFetcher& operator=(ShaderFetcher&&) = delete;

    /// Begins the task.
    Result<> Begin();

    /// Updates the task.  This must be called periodically while IsPending() returns true.
    /// In addition this task depends on the FileFetcher to be updated periodically, so the
    /// caller must ensure that the FileFetcher is updated as well.
    void Update();

    /// Returns true if the task is running (started but not complete).
    bool IsPending() const;

    /// Returns the shader module if the task succeeded, otherwise returns an error.
    /// This method will invalidate the task, so it can only be called once.
    Result<wgpu::ShaderModule> Take();

private:

    enum class Stage
    {
        None,
        Fetching,
        Succeeded,
        Failed
    };

    Result<> CreateShaderModule();

    std::string m_Path;
    const GpuHelper* m_GpuHelper{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    uint64_t m_RequestId;
    std::vector<uint8_t> m_ShaderData;
    wgpu::ShaderModule m_ShaderModule;

    Stage m_Stage{ Stage::None };
};