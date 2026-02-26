#pragma once

#include "Result.h"
#include "Vertex.h"

#include <span>

#define DAWN_GPU 1
//#define DAWN_GPU 0

#define OFFSCREEN_RENDERING 0

template<typename T>
class RgbaColor;
using RgbaColorf = RgbaColor<float>;
class RenderCompositor;
class Renderer;
class MaterialConstants;

/// @brief GPU representation of a vertex buffer.
class GpuVertexBuffer
{
public:

    virtual unsigned GetVertexCount() const = 0;

protected:
    GpuVertexBuffer() = default;
    virtual ~GpuVertexBuffer() = 0;
};

/// @brief GPU representation of an index buffer.
class GpuIndexBuffer
{
public:

    virtual unsigned GetIndexCount() const = 0;

protected:
    GpuIndexBuffer() = default;
    virtual ~GpuIndexBuffer() = 0;
};

/// @brief GPU representation of a texture.
class GpuTexture
{
public:

    virtual unsigned GetWidth() const = 0;
    virtual unsigned GetHeight() const = 0;

protected:
    GpuTexture() = default;
    virtual ~GpuTexture() = 0;
};

/// @brief GPU representation of a material.
class GpuMaterial
{
public:

    virtual GpuTexture* GetBaseTexture() const = 0;
    virtual const MaterialConstants& GetConstants() const = 0;

protected:
    GpuMaterial() = default;
    virtual ~GpuMaterial() = 0;
};

/// @brief GPU representation of a color render target.
class GpuColorTarget
{
public:

    virtual unsigned GetWidth() const = 0;
    virtual unsigned GetHeight() const = 0;

protected:
    GpuColorTarget() = default;
    virtual ~GpuColorTarget() = 0;
};

/// @brief GPU representation of a depth target.
class GpuDepthTarget
{
public:

    virtual unsigned GetWidth() const = 0;
    virtual unsigned GetHeight() const = 0;

protected:
    GpuDepthTarget() = default;
    virtual ~GpuDepthTarget() = 0;
};

/// @brief GPU representation of a render pass.
class GpuRenderPass
{
protected:
    GpuRenderPass() = default;
    virtual ~GpuRenderPass() = 0;
};

/// @brief Abstract base class for GPU device implementation.
class GpuDevice
{
public:
    GpuDevice(const GpuDevice&) = delete;
    GpuDevice& operator=(const GpuDevice&) = delete;
    GpuDevice(GpuDevice&&) = delete;
    GpuDevice& operator=(GpuDevice&&) = delete;

    /// @brief Gets the renderable extent of the device.
    virtual Extent GetScreenBounds() const = 0;

    /// @brief Creates a vertex buffer from the given vertices.
    virtual Result<GpuVertexBuffer*> CreateVertexBuffer(const std::span<const Vertex>& vertices) = 0;

    /// @brief Creates a vertex buffer from multiple spans of vertices.
    virtual Result<GpuVertexBuffer*> CreateVertexBuffer(
        const std::span<std::span<const Vertex>>& vertices) = 0;

    /// @brief Destroys a vertex buffer.
    virtual Result<void> DestroyVertexBuffer(GpuVertexBuffer* vertexBuffer) = 0;

    /// @brief Creates an index buffer from the given indices.
    virtual Result<GpuIndexBuffer*> CreateIndexBuffer(const std::span<const VertexIndex>& indices) = 0;

    /// @brief Creates an index buffer from multiple spans of indices.
    virtual Result<GpuIndexBuffer*> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) = 0;

    /// @brief Destroys an index buffer.
    virtual Result<void> DestroyIndexBuffer(GpuIndexBuffer* indexBuffer) = 0;

    /// @brief Creates a texture from raw pixel data.
    /// Pixels are expected to be in RGBA8 format.
    /// rowStride is the number of bytes between the start of each row.
    /// rowStride must be at least width * 4.
    virtual Result<GpuTexture*> CreateTexture(const unsigned width,
        const unsigned height,
        const uint8_t* pixels,
        const unsigned rowStride,
        const imstring& name) = 0;

    /// @brief Creates a 1x1 texture from a color.
    virtual Result<GpuTexture*> CreateTexture(const RgbaColorf& color, const imstring& name) = 0;

    /// @brief Destroys a texture.
    virtual Result<void> DestroyTexture(GpuTexture* texture) = 0;

    /// @brief Creates a color render target with the given dimensions and name.
    virtual Result<GpuColorTarget*> CreateColorTarget(
        const unsigned width, const unsigned height, const imstring& name) = 0;

    /// @brief Destroys a color render target.
    virtual Result<void> DestroyColorTarget(GpuColorTarget* colorTarget) = 0;

    virtual Result<GpuDepthTarget*> CreateDepthTarget(const unsigned width,
        const unsigned height,
        const imstring& name) = 0;

    /// @brief Destroys a depth target.
    virtual Result<void> DestroyDepthTarget(GpuDepthTarget* depthTarget) = 0;

    virtual Result<GpuMaterial*> CreateMaterial(const MaterialConstants& constants,
        GpuTexture* baseTexture) = 0;

    virtual Result<void> DestroyMaterial(GpuMaterial* material) = 0;

    virtual Renderer* GetRenderer() = 0;

    virtual RenderCompositor* GetRenderCompositor() = 0;

protected:
    GpuDevice() = default;

    virtual ~GpuDevice() = 0;
};

inline GpuVertexBuffer::~GpuVertexBuffer() = default;
inline GpuIndexBuffer::~GpuIndexBuffer() = default;
inline GpuTexture::~GpuTexture() = default;
inline GpuMaterial::~GpuMaterial() = default;
inline GpuColorTarget::~GpuColorTarget() = default;
inline GpuDepthTarget::~GpuDepthTarget() = default;
inline GpuRenderPass::~GpuRenderPass() = default;
inline GpuDevice::~GpuDevice() = default;
