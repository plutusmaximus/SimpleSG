#pragma once

#include "Renderer.h"
#include "Result.h"
#include "VecMath.h"
#include "Vertex.h"

#include <memory>
#include <span>
#include <tuple>
#include <variant>

template<typename T>
class RgbaColor;
using RgbaColorf = RgbaColor<float>;

enum class GpuPipelineType
{
    Opaque
};

/// @brief GPU representation of a vertex buffer.
class GpuVertexBuffer
{
public:

    class Subrange
    {
    public:

        bool IsValid() const { return m_Owner != nullptr; }

        GpuVertexBuffer* GetBuffer() { return m_Owner; }

        const GpuVertexBuffer* GetBuffer() const { return m_Owner; }

        /// @brief Offset in bytes of first item in buffer.
        uint32_t GetByteOffset() const { return m_ByteOffset; }

        /// @brief Number of items in buffer.
        uint32_t GetItemCount() const { return m_ItemCount; }

    protected:

        Subrange(GpuVertexBuffer* owner, const uint32_t itemOffset, const uint32_t itemCount)
            : m_Owner(owner), m_ByteOffset(itemOffset * sizeof(Vertex)), m_ItemCount(itemCount)
        {
        }
    private:

        // This pointer is borrowed from the parent VertexBuffer.
        // It must not outlive its parent.
        GpuVertexBuffer* m_Owner{ nullptr };
        uint32_t m_ByteOffset{ 0 };
        uint32_t m_ItemCount{ 0 };
    };

    virtual Subrange GetSubrange(const uint32_t itemOffset, const uint32_t itemCount) = 0;

protected:
    GpuVertexBuffer() = default;
    virtual ~GpuVertexBuffer() = 0;
};

/// @brief GPU representation of an index buffer.
class GpuIndexBuffer
{
public:

    class Subrange
    {
    public:

        bool IsValid() const { return m_Owner != nullptr; }

        GpuIndexBuffer* GetBuffer() { return m_Owner; }

        const GpuIndexBuffer* GetBuffer() const { return m_Owner; }

        /// @brief Offset in bytes of first item in buffer.
        uint32_t GetByteOffset() const { return m_ByteOffset; }

        /// @brief Number of items in buffer.
        uint32_t GetItemCount() const { return m_ItemCount; }

    protected:

        Subrange(GpuIndexBuffer* owner, const uint32_t itemOffset, const uint32_t itemCount)
            : m_Owner(owner), m_ByteOffset(itemOffset * sizeof(VertexIndex)), m_ItemCount(itemCount)
        {
        }
    private:

        // This pointer is borrowed from the parent VertexBuffer.
        // It must not outlive its parent.
        GpuIndexBuffer* m_Owner{ nullptr };
        uint32_t m_ByteOffset{ 0 };
        uint32_t m_ItemCount{ 0 };
    };

    virtual Subrange GetSubrange(const uint32_t itemOffset, const uint32_t itemCount) = 0;

protected:
    GpuIndexBuffer() = default;
    virtual ~GpuIndexBuffer() = 0;
};

/// @brief GPU representation of a vertex shader.
class GpuVertexShader
{
protected:
    GpuVertexShader() = default;
    virtual ~GpuVertexShader() = 0;
};

/// @brief GPU representation of a fragment shader.
class GpuFragmentShader
{
protected:
    GpuFragmentShader() = default;
    virtual ~GpuFragmentShader() = 0;
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

/// @brief GPU representation of a pipeline state.
class GpuPipeline
{
protected:
    GpuPipeline() = default;
    virtual ~GpuPipeline() = 0;
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

    /// @brief Gets the renderable extent of the device.
    virtual Extent GetExtent() const = 0;

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

    /// @brief Creates a vertex shader from the given specification.
    virtual Result<GpuVertexShader*> CreateVertexShader(const std::span<const uint8_t>& shaderByteCode) = 0;

    /// @brief Destroys a vertex shader.
    virtual Result<void> DestroyVertexShader(GpuVertexShader* vertexShader) = 0;

    /// @brief Creates a fragment shader from the given specification.
    virtual Result<GpuFragmentShader*> CreateFragmentShader(const std::span<const uint8_t>& shaderByteCode) = 0;

    /// @brief Destroys a fragment shader.
    virtual Result<void> DestroyFragmentShader(GpuFragmentShader* fragmentShader) = 0;

    virtual Result<GpuPipeline*> CreatePipeline(const GpuPipelineType pipelineType,
        GpuVertexShader* vertexShader,
        GpuFragmentShader* fragmentShader) = 0;

    virtual Result<void> DestroyPipeline(GpuPipeline* pipeline) = 0;

    virtual Result<Renderer*> CreateRenderer(GpuPipeline* pipeline) = 0;

    virtual void DestroyRenderer(Renderer* renderer) = 0;

protected:
    GpuDevice() = default;

    virtual ~GpuDevice() = 0;
};

inline GpuVertexBuffer::~GpuVertexBuffer() = default;
inline GpuIndexBuffer::~GpuIndexBuffer() = default;
inline GpuVertexShader::~GpuVertexShader() = default;
inline GpuFragmentShader::~GpuFragmentShader() = default;
inline GpuTexture::~GpuTexture() = default;
inline GpuColorTarget::~GpuColorTarget() = default;
inline GpuDepthTarget::~GpuDepthTarget() = default;
inline GpuPipeline::~GpuPipeline() = default;
inline GpuRenderPass::~GpuRenderPass() = default;
inline GpuDevice::~GpuDevice() = default;
