#pragma once

#include "RenderGraph.h"
#include "RefCount.h"
#include "Error.h"
#include "VecMath.h"
#include "Vertex.h"
#include <span>
#include <tuple>
#include <variant>

template<typename T> class RgbaColor;
using RgbaColorf = RgbaColor<float>;
class Image;
class VertexShaderSpec;
class FragmentShaderSpec;

/// @brief GPU representation of a vertex buffer.
class GpuVertexBuffer
{
public:

    virtual ~GpuVertexBuffer() = 0 {}
protected:

    GpuVertexBuffer() = default;
    IMPLEMENT_REFCOUNT(GpuVertexBuffer);
};

/// @brief GPU representation of an index buffer.
class GpuIndexBuffer
{
public:

    virtual ~GpuIndexBuffer() = 0 {}
protected:

    GpuIndexBuffer() = default;
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
protected:

    GpuTexture() {}

    virtual ~GpuTexture() = 0 {}
};

/// @brief API representation of a vertex buffer.
/// Contains a reference to the underlying GPU buffer and metadata about the buffer's layout.
class VertexBuffer
{
public:

    VertexBuffer() = default;
    VertexBuffer(RefPtr<GpuVertexBuffer> buffer, const uint32_t itemOffset, const uint32_t itemCount)
        : m_Buffer(buffer)
        , ByteOffset(itemOffset * sizeof(Vertex))
        , ItemCount(itemCount)
    {
    }

    /// @brief Retrieves a sub-range buffer from this buffer.
    Result<VertexBuffer> GetSubRange(
        const uint32_t itemOffset,
        const uint32_t itemCount)
    {
        expect(IsValid(), "Invalid buffer");
        expect(itemOffset + itemCount <= this->ItemCount, "Sub-range out of bounds");

        return VertexBuffer(m_Buffer, itemOffset, itemCount);
    }

    template<typename T>
    T* Get() { return m_Buffer.Get<T>(); }
    
    template<typename T>
    const T* Get() const { return m_Buffer.Get<T>(); }

    bool IsValid() const { return m_Buffer != nullptr; }

    /// @brief Offset in bytes of first item in buffer.
    const uint32_t ByteOffset;

    /// @brief Number of items in buffer.
    const uint32_t ItemCount;

private:

    RefPtr<GpuVertexBuffer> m_Buffer;
};

/// @brief API representation of an index buffer.
/// Contains a reference to the underlying GPU buffer and metadata about the buffer's layout.
class IndexBuffer
{
public:

    IndexBuffer() = default;
    IndexBuffer(RefPtr<GpuIndexBuffer> buffer, const uint32_t itemOffset, const uint32_t itemCount)
        : m_Buffer(buffer)
        , ByteOffset(itemOffset * sizeof(VertexIndex))
        , ItemCount(itemCount)
    {
    }

    /// @brief Retrieves a sub-range buffer from this buffer.
    Result<IndexBuffer> GetSubRange(
        const uint32_t itemOffset,
        const uint32_t itemCount)
    {
        expect(IsValid(), "Invalid buffer");
        expect(itemOffset + itemCount <= this->ItemCount, "Sub-range out of bounds");

        return IndexBuffer(m_Buffer, itemOffset, itemCount);
    }

    template<typename T>
    T* Get() { return m_Buffer.Get<T>(); }
    
    template<typename T>
    const T* Get() const { return m_Buffer.Get<T>(); }

    bool IsValid() const { return m_Buffer != nullptr; }

    /// @brief Offset in bytes of first item in buffer.
    const uint32_t ByteOffset;

    /// @brief Number of items in buffer.
    const uint32_t ItemCount;

private:

    RefPtr<GpuIndexBuffer> m_Buffer;
};

/// @brief API representation of a vertex shader.
/// Contains a reference to the underlying GPU vertex shader.
class VertexShader
{
public:

    VertexShader() = default;

    explicit VertexShader(RefPtr<GpuVertexShader> shader)
        : m_Shader(shader)
    {
    }

    template<typename T>
    T* Get() { return m_Shader.Get<T>(); }

    template<typename T>
    const T* Get() const { return m_Shader.Get<T>(); }

    bool IsValid() const { return m_Shader != nullptr; }

private:

    RefPtr<GpuVertexShader> m_Shader;
};

/// @brief API representation of a fragment shader.
/// Contains a reference to the underlying GPU fragment shader.
class FragmentShader
{
public:

    FragmentShader() = default;

    explicit FragmentShader(RefPtr<GpuFragmentShader> shader)
        : m_Shader(shader)
    {
    }

    template<typename T>
    T* Get() { return m_Shader.Get<T>(); }

    template<typename T>
    const T* Get() const { return m_Shader.Get<T>(); }

    bool IsValid() const { return m_Shader != nullptr; }

private:

    RefPtr<GpuFragmentShader> m_Shader;
};

/// @brief API representation of a texture.
/// Contains a reference to the underlying GPU texture.
class Texture
{
public:

    Texture() = default;

    explicit Texture(GpuTexture* texture)
        : m_Texture(texture)
    {
    }

    template<typename T>
    T* Get() { return static_cast<T*>(m_Texture); }

    template<typename T>
    const T* Get() const { return static_cast<const T*>(m_Texture); }

    bool IsValid() const { return m_Texture != nullptr; }

private:

    GpuTexture* m_Texture{ nullptr };
};

/// @brief Abstract base class for GPU device implementation.
class GPUDevice
{
public:

    virtual ~GPUDevice() = 0 {};

    /// @brief Gets the renderable extent of the device.
    virtual Extent GetExtent() const = 0;

    /// @brief Creates a vertex buffer from the given vertices.
    virtual Result<VertexBuffer> CreateVertexBuffer(
        const std::span<const Vertex>& vertices) = 0;

    /// @brief Creates a vertex buffer from multiple spans of vertices.
    virtual Result<VertexBuffer> CreateVertexBuffer(
        const std::span<std::span<const Vertex>>& vertices) = 0;

    /// @brief Creates an index buffer from the given indices.
    virtual Result<IndexBuffer> CreateIndexBuffer(
        const std::span<const VertexIndex>& indices) = 0;

    /// @brief Creates an index buffer from multiple spans of indices.
    virtual Result<IndexBuffer> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) = 0;

    /// @brief Creates a texture from an image.
    virtual Result<Texture> CreateTexture(const Image& image) = 0;

    /// @brief Creates a 1x1 texture from a color.
    virtual Result<Texture> CreateTexture(const RgbaColorf& color) = 0;

    /// @brief Destroys a texture.
    virtual Result<void> DestroyTexture(Texture& texture) = 0;

    /// @brief Creates a vertex shader from the given specification.
    virtual Result<VertexShader> CreateVertexShader(const VertexShaderSpec& shaderSpec) = 0;

    /// @brief Creates a fragment shader from the given specification.
    virtual Result<FragmentShader> CreateFragmentShader(const FragmentShaderSpec& shaderSpec) = 0;

    virtual Result<RenderGraph*> CreateRenderGraph() = 0;

    virtual void DestroyRenderGraph(RenderGraph* renderGraph) = 0;
};