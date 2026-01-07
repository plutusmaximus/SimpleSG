#pragma once

#include "RefCount.h"
#include "Error.h"
#include "VecMath.h"
#include "Vertex.h"
#include "Material.h"
#include <span>
#include <tuple>
#include <variant>

/// @brief GPU representation of a vertex buffer.
class GpuVertexBuffer
{
public:

    GpuVertexBuffer() = delete;

    virtual ~GpuVertexBuffer() = 0 {}

    /// @brief Retrieves a sub-range vertex buffer from this buffer.
    virtual Result<RefPtr<GpuVertexBuffer>> GetSubRange(
        const uint32_t itemOffset,
        const uint32_t itemCount) = 0;

    /// @brief Offset of first vertex in buffer, in bytes.
    const uint32_t ByteOffset;

    /// @brief Number of vertices in buffer.
    const uint32_t ItemCount;

protected:

    GpuVertexBuffer(const uint32_t itemOffset, const uint32_t itemCount)
        : ByteOffset(itemOffset * sizeof(Vertex))
        , ItemCount(itemCount)
    {
    }

    IMPLEMENT_REFCOUNT(GpuVertexBuffer);
};

/// @brief GPU representation of an index buffer.
class GpuIndexBuffer
{
public:

    GpuIndexBuffer() = delete;

    virtual ~GpuIndexBuffer() = 0 {}

    /// @brief Retrieves a sub-range index buffer from this buffer.
    virtual Result<RefPtr<GpuIndexBuffer>> GetSubRange(
        const uint32_t itemOffset,
        const uint32_t itemCount) = 0;

    /// @brief Offset of first index in buffer, in bytes.
    const uint32_t ByteOffset;

    /// @brief Number of indices in buffer.
    const uint32_t ItemCount;

protected:

    GpuIndexBuffer(const uint32_t itemOffset, const uint32_t itemCount)
        : ByteOffset(itemOffset * sizeof(VertexIndex))
        , ItemCount(itemCount)
    {
    }

    IMPLEMENT_REFCOUNT(GpuIndexBuffer);
};

/// @brief GPU representation of a vertex shader.
class GpuVertexShader
{
public:

    GpuVertexShader() {}
    
    virtual ~GpuVertexShader() = 0 {}

    IMPLEMENT_REFCOUNT(GpuVertexShader);
};

/// @brief GPU representation of a fragment shader.
class GpuFragmentShader
{
public:

    GpuFragmentShader() {}
    
    virtual ~GpuFragmentShader() = 0 {}

    IMPLEMENT_REFCOUNT(GpuFragmentShader);
};

/// @brief GPU representation of a texture.
class GpuTexture
{
public:

    GpuTexture() {}
    
    virtual ~GpuTexture() = 0 {}

    IMPLEMENT_REFCOUNT(GpuTexture);
};

/// @brief Abstract base class for GPU devices.
class GPUDevice
{
public:

    GPUDevice() {}

    virtual ~GPUDevice() = 0 {}

    /// @brief Gets the renderable extent of the device.
    virtual Extent GetExtent() const = 0;

    /// @brief Creates an index buffer from the given indices.
    virtual Result<RefPtr<GpuIndexBuffer>> CreateIndexBuffer(
        const std::span<const VertexIndex>& indices) = 0;

    /// @brief Creates a vertex buffer from the given vertices.
    virtual Result<RefPtr<GpuVertexBuffer>> CreateVertexBuffer(
        const std::span<const Vertex>& vertices) = 0;

    /// @brief Creates an index buffer from multiple spans of indices.
    virtual Result<RefPtr<GpuIndexBuffer>> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) = 0;

    /// @brief Creates a vertex buffer from multiple spans of vertices.
    virtual Result<RefPtr<GpuVertexBuffer>> CreateVertexBuffer(
        const std::span<std::span<const Vertex>>& vertices) = 0;

    /// @brief Creates a texture from the given specification.
    virtual Result<RefPtr<GpuTexture>> CreateTexture(const TextureSpec& textureSpec) = 0;

    /// @brief Creates a vertex shader from the given specification.
    virtual Result<RefPtr<GpuVertexShader>> CreateVertexShader(const VertexShaderSpec& shaderSpec) = 0;

    /// @brief Creates a fragment shader from the given specification.
    virtual Result<RefPtr<GpuFragmentShader>> CreateFragmentShader(const FragmentShaderSpec& shaderSpec) = 0;

    IMPLEMENT_REFCOUNT(GPUDevice);
};