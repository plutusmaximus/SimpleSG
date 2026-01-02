#pragma once

#include "RefCount.h"
#include "Error.h"
#include "VecMath.h"

class Model;
class RenderGraph;
class ModelSpec;

class GPUDevice
{
public:

    static constexpr const char* WHITE_TEXTURE_KEY = "$white";
    static constexpr const char* MAGENTA_TEXTURE_KEY = "$magenta";

    GPUDevice() {}

    virtual ~GPUDevice() = 0 {}

    /// @brief Creates a model from the given specification.
    virtual Result<RefPtr<Model>> CreateModel(const ModelSpec& modelSpec) = 0;

    /// @brief Gets the renderable extent of the device.
    virtual Extent GetExtent() const = 0;

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