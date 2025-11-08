#pragma once

#include "RefCount.h"
#include "Error.h"

class Model;
class RenderGraph;
class ModelSpec;

class GPUDevice
{
public:

    GPUDevice() {}

    virtual ~GPUDevice() = 0 {}

    virtual Result<RefPtr<Model>> CreateModel(const ModelSpec& modelSpec) = 0;

    virtual Result<RefPtr<RenderGraph>> CreateRenderGraph() = 0;

    IMPLEMENT_REFCOUNT(GPUDevice);
};

class GpuBuffer
{
public:

    GpuBuffer() {}

    virtual ~GpuBuffer() = 0 {}

    IMPLEMENT_REFCOUNT(GpuBuffer);
};

class VertexBuffer
{
public:

    VertexBuffer() = delete;

    VertexBuffer(RefPtr<GpuBuffer> gpuBuffer, const uint32_t offset)
        : GpuBuffer(gpuBuffer)
        , Offset(offset)
    {
    }

    RefPtr<GpuBuffer> const GpuBuffer;
    const uint32_t Offset;
};

class IndexBuffer
{
public:

    IndexBuffer() = delete;

    IndexBuffer(RefPtr<GpuBuffer> gpuBuffer, const uint32_t offset)
        : GpuBuffer(gpuBuffer)
        , Offset(offset)
    {
    }

    RefPtr<GpuBuffer> const GpuBuffer;
    const uint32_t Offset;
};