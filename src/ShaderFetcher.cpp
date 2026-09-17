#define MLG_LOGGER_NAME "SHDR"

#include "ShaderFetcher.h"

#include "FileFetcher.h"
#include "GpuHelper.h"

#include <filesystem>
#include <webgpu/webgpu_cpp.h>

ShaderFetcher::ShaderFetcher(
    std::string path, const GpuHelper& gpuHelper, FileFetcher& fileFetcher)
    : m_Path(std::move(path)),
      m_GpuHelper(&gpuHelper),
      m_FileFetcher(&fileFetcher),
      m_RequestId(FileFetcher::kInvalidRequestId)
{
}

ShaderFetcher::~ShaderFetcher()
{
    MLG_ASSERT(Stage::None == m_Stage || !IsPending(), "Destroying pending task");
}

Result<>
ShaderFetcher::Begin()
{
    MLG_LOG_SCOPE(m_Path);

    MLG_CHECKV(Stage::None == m_Stage, "Task has already been started");
    
    MLG_INFO("Loading shader...");

    m_Stage = Stage::Failed;

    auto requestId = m_FileFetcher->Fetch(std::string(m_Path));
    MLG_CHECK(requestId);

    m_RequestId = *requestId;

    m_Stage = Stage::Fetching;

    return Result<>::Ok;
}

void
ShaderFetcher::Update()
{
    MLG_LOG_SCOPE(m_Path);

    if(!MLG_VERIFY(IsPending(), "Task is not running"))
    {
        return;
    }

    switch(m_Stage)
    {
        case Stage::None:
            break;
        case Stage::Fetching:
            if(!m_FileFetcher->IsPending(m_RequestId))
            {
                if(m_FileFetcher->Take(m_RequestId, m_ShaderData))
                {
                    if(!CreateShaderModule())
                    {
                        MLG_ERROR("Failed to create shader module: {}", m_Path);
                        m_Stage = Stage::Failed;
                    }
                    else
                    {
                        MLG_DEBUG("Loaded shader: {}", m_Path);
                        m_Stage = Stage::Succeeded;
                    }
                }
                else
                {
                    m_Stage = Stage::Failed;
                }
            }
            break;
        case Stage::Succeeded:
        case Stage::Failed:
            break;
    }
}

bool
ShaderFetcher::IsPending() const
{
    return MLG_VERIFY(Stage::None != m_Stage, "Task is not started")
        && Stage::Succeeded != m_Stage
        && Stage::Failed != m_Stage;
}

Result<wgpu::ShaderModule>
ShaderFetcher::Take()
{
    MLG_CHECKV(Stage::Succeeded == m_Stage, "Task has not succeeded");

    MLG_CHECKV(m_ShaderModule, "Shader module already consumed");

    wgpu::ShaderModule shaderModule = m_ShaderModule;
    m_ShaderModule = nullptr; // Invalidate the shader module so it can only be taken once

    return shaderModule;
}

Result<>
ShaderFetcher::CreateShaderModule()
{
    const std::string filename = std::filesystem::path(m_Path).filename().string();

    const void* dataPtr = m_ShaderData.data();
    const wgpu::StringView shaderCode{ static_cast<const char*>(dataPtr), m_ShaderData.size() };
    const wgpu::StringView label = std::string_view(filename);
    const wgpu::ShaderSourceWGSL wgsl{ { .code = shaderCode } };
    const wgpu::ShaderModuleDescriptor desc{ .nextInChain = &wgsl, .label = label };

    MLG_INFO("Creating shader module...");

    m_ShaderModule = m_GpuHelper->GetDevice().CreateShaderModule(&desc);
    MLG_CHECK(m_ShaderModule, "Failed to create shader module");

    return Result<>::Ok;
}