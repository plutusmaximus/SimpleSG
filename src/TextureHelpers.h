#pragma once

#include "CoopTask.h"
#include "FileFetcher.h"
#include "FilePath.h"

#include <atomic>
#include <webgpu/webgpu_cpp.h>

class GpuHelper;
class ThreadPool;

/// Task responsible for decoding a texture from raw image data.
/// A wgpu::CommandEncoder must be provided to record the necessary GPU commands for texture upload.
/// This task records the necessary GPU commands to the command encoderfor texture upload.
/// The caller is responsible for submitting the recorded command buffer to the GPU queue.
class TextureDecodeTask : public ICoopTask<>
{
public:
    TextureDecodeTask(const GpuHelper& gpuHelper,
        std::vector<std::byte>&& rawData,
        const FilePath& path,
        ThreadPool& threadPool,
        wgpu::CommandEncoder commandEncoder);

    TextureDecodeTask() = delete;
    ~TextureDecodeTask() override = default; // The base class will verify completion
    TextureDecodeTask(const TextureDecodeTask&) = delete;
    TextureDecodeTask& operator=(const TextureDecodeTask&) = delete;
    TextureDecodeTask(TextureDecodeTask&&) = delete;
    TextureDecodeTask& operator=(TextureDecodeTask&&) = delete;

    Result<wgpu::Texture> Take();

private:
    enum class Stage
    {
        None,
        Decoding,
        Succeeded,
        Failed
    };

    Result<> OnStart() override;

    void OnUpdate() override;

    Result<> BeginDecode();

    Result<> Decode();

    // Worker thread entry point for decoding the texture.
    static void Decode(void* userData);

    Result<> CommitStagingBuffer();

    const GpuHelper* m_GpuHelper{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    FilePath m_Path;
    std::vector<std::byte> m_TexBytes;
    wgpu::Texture m_Texture{ nullptr };
    wgpu::Buffer m_StagingBuffer{ nullptr };
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };
    std::span<std::byte> m_MappedMemory;
    Result<> m_DecodeResult;

    // We cannot access webgpu objects directly in the worker thread,
    // so we cache the necessary properties here.
    uint32_t m_ExpectedTexWidth{ 0 };
    uint32_t m_ExpectedTexHeight{ 0 };

    std::atomic<bool> m_CompletionFlag{ false };

    Stage m_Stage{ Stage::None };
};

/// Task responsible for fetching and decoding a texture.
/// Callers should repeatedly call FileFetcher::ProcessCompletions() while this task is running.
/// A wgpu::CommandEncoder must be provided to record the necessary GPU commands for texture upload.
class TextureFetchTask : public ICoopTask<>
{
public:
    TextureFetchTask(const GpuHelper& gpuHelper,
        FileFetcher& fileFetcher,
        ThreadPool& threadPool,
        const FilePath& path,
        wgpu::CommandEncoder commandEncoder);

    TextureFetchTask() = delete;
    ~TextureFetchTask() override = default; // The base class will verify completion
    TextureFetchTask(const TextureFetchTask&) = delete;
    TextureFetchTask& operator=(const TextureFetchTask&) = delete;
    TextureFetchTask(TextureFetchTask&&) = delete;
    TextureFetchTask& operator=(TextureFetchTask&&) = delete;

    Result<wgpu::Texture> Take();

private:
    enum class Stage
    {
        None,
        Fetching,
        Decoding,
        Succeeded,
        Failed
    };

    Result<> OnStart() override;

    void OnUpdate() override;

    const GpuHelper* m_GpuHelper{ nullptr };
    FileFetcher* m_FileFetcher{ nullptr };
    ThreadPool* m_ThreadPool{ nullptr };
    FilePath m_Path;
    FetchRequestId m_FetchRequestId;
    wgpu::Texture m_Texture{ nullptr };
    wgpu::CommandEncoder m_CommandEncoder{ nullptr };

    std::optional<TextureDecodeTask> m_TextureDecodeTask;

    Stage m_Stage{ Stage::None };
};