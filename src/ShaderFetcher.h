#pragma once

#include "CoopTask.h"
#include "FileFetcher.h"
#include "FixedString.h"
#include "Result.h"

#include <cstdint>
#include <vector>
#include <webgpu/webgpu_cpp.h>

class GpuHelper;
class FileFetcher;

/// A task that fetches a shader from disk and creates a wgpu::ShaderModule.
/// Keep calling FileFetcher::ProcessCompletions() while this task is running.
/// Calling Update() alone does not process file completions.
class ShaderFetcher : public ICoopTask<>
{
public:

    ShaderFetcher(const std::string_view path, const GpuHelper& gpuHelper, FileFetcher& fileFetcher);

    ShaderFetcher() = delete;
    ~ShaderFetcher() override = default;
    ShaderFetcher(const ShaderFetcher&) = delete;
    ShaderFetcher& operator=(const ShaderFetcher&) = delete;
    ShaderFetcher(ShaderFetcher&&) = delete;
    ShaderFetcher& operator=(ShaderFetcher&&) = delete;

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

    Result<> OnStart() override;

    void OnUpdate() override;

    Result<> CreateShaderModule();

    constexpr static size_t kMaxPathLen = 256; // Adjust as needed
    FixedString<kMaxPathLen> m_Path;
    const GpuHelper* m_GpuHelper{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    FetchRequestId m_RequestId;
    std::vector<uint8_t> m_ShaderData;
    wgpu::ShaderModule m_ShaderModule;

    Stage m_Stage{ Stage::None };
};