#pragma once

#include "RenderGraph.h"
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
protected:

    GpuVertexBuffer() {}
    virtual ~GpuVertexBuffer() = 0 {}
};

/// @brief GPU representation of an index buffer.
class GpuIndexBuffer
{
protected:

    GpuIndexBuffer() {}
    virtual ~GpuIndexBuffer() = 0 {}
};

/// @brief GPU representation of a vertex shader.
class GpuVertexShader
{
protected:

    GpuVertexShader() {}    
    virtual ~GpuVertexShader() = 0 {}
};

/// @brief GPU representation of a fragment shader.
class GpuFragmentShader
{
protected:

    GpuFragmentShader() {}    
    virtual ~GpuFragmentShader() = 0 {}
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
    VertexBuffer(GpuVertexBuffer* buffer, const uint32_t itemOffset, const uint32_t itemCount)
        : m_Buffer(buffer)
        , m_ByteOffset(itemOffset * sizeof(Vertex))
        , m_ItemCount(itemCount)
    {
    }

    /// @brief Retrieves a sub-range buffer from this buffer.
    Result<VertexBuffer> GetSubRange(
        const uint32_t itemOffset,
        const uint32_t itemCount)
    {
        expect(IsValid(), "Invalid buffer");
        expect(itemOffset + itemCount <= m_ItemCount, "Sub-range out of bounds");

        return VertexBuffer(m_Buffer, itemOffset, itemCount);
    }

    GpuVertexBuffer* Get() { return m_Buffer; }
    const GpuVertexBuffer* Get() const { return m_Buffer; }

    template<typename T>
    T* Get() { return static_cast<T*>(m_Buffer); }
    
    template<typename T>
    const T* Get() const { return static_cast<const T*>(m_Buffer); }

    bool IsValid() const { return m_Buffer != nullptr; }

    /// @brief Offset in bytes of first item in buffer.
    uint32_t GetByteOffset() const { return m_ByteOffset; }

    /// @brief Number of items in buffer.
    uint32_t GetItemCount() const { return m_ItemCount; }

private:

    GpuVertexBuffer* m_Buffer{nullptr};

    uint32_t m_ByteOffset{0};

    uint32_t m_ItemCount{0};
};

/// @brief API representation of an index buffer.
/// Contains a reference to the underlying GPU buffer and metadata about the buffer's layout.
class IndexBuffer
{
public:

    IndexBuffer() = default;
    IndexBuffer(GpuIndexBuffer* buffer, const uint32_t itemOffset, const uint32_t itemCount)
        : m_Buffer(buffer)
        , m_ByteOffset(itemOffset * sizeof(VertexIndex))
        , m_ItemCount(itemCount)
    {
    }

    /// @brief Retrieves a sub-range buffer from this buffer.
    Result<IndexBuffer> GetSubRange(
        const uint32_t itemOffset,
        const uint32_t itemCount)
    {
        expect(IsValid(), "Invalid buffer");
        expect(itemOffset + itemCount <= m_ItemCount, "Sub-range out of bounds");

        return IndexBuffer(m_Buffer, itemOffset, itemCount);
    }

    GpuIndexBuffer* Get() { return m_Buffer; }
    const GpuIndexBuffer* Get() const { return m_Buffer; }

    template<typename T>
    T* Get() { return static_cast<T*>(m_Buffer); }
    
    template<typename T>
    const T* Get() const { return static_cast<const T*>(m_Buffer); }

    bool IsValid() const { return m_Buffer != nullptr; }

    uint32_t GetByteOffset() const { return m_ByteOffset; }

    uint32_t GetItemCount() const { return m_ItemCount; }

private:

    GpuIndexBuffer* m_Buffer{nullptr};

    uint32_t m_ByteOffset{0};

    uint32_t m_ItemCount{0};
};

/// @brief API representation of a vertex shader.
/// Contains a reference to the underlying GPU vertex shader.
class VertexShader
{
public:

    VertexShader() = default;

    explicit VertexShader(GpuVertexShader* shader)
        : m_Shader(shader)
    {
    }

    GpuVertexShader* Get() { return m_Shader; }
    const GpuVertexShader* Get() const { return m_Shader; }

    template<typename T>
    T* Get() { return static_cast<T*>(m_Shader); }

    template<typename T>
    const T* Get() const { return static_cast<const T*>(m_Shader); }

    bool IsValid() const { return m_Shader != nullptr; }

private:

    GpuVertexShader* m_Shader{ nullptr };
};

/// @brief API representation of a fragment shader.
/// Contains a reference to the underlying GPU fragment shader.
class FragmentShader
{
public:

    FragmentShader() = default;

    explicit FragmentShader(GpuFragmentShader* shader)
        : m_Shader(shader)
    {
    }

    GpuFragmentShader* Get() { return m_Shader; }
    const GpuFragmentShader* Get() const { return m_Shader; }

    template<typename T>
    T* Get() { return static_cast<T*>(m_Shader); }

    template<typename T>
    const T* Get() const { return static_cast<const T*>(m_Shader); }

    bool IsValid() const { return m_Shader != nullptr; }

private:

    GpuFragmentShader* m_Shader{ nullptr };
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

    GpuTexture* Get() { return m_Texture; }
    const GpuTexture* Get() const { return m_Texture; }

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

    /// @brief Destroys a vertex buffer.
    virtual Result<void> DestroyVertexBuffer(VertexBuffer& buffer) = 0;

    /// @brief Creates an index buffer from the given indices.
    virtual Result<IndexBuffer> CreateIndexBuffer(
        const std::span<const VertexIndex>& indices) = 0;

    /// @brief Creates an index buffer from multiple spans of indices.
    virtual Result<IndexBuffer> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) = 0;

    /// @brief Destroys an index buffer.
    virtual Result<void> DestroyIndexBuffer(IndexBuffer& buffer) = 0;

    /// @brief Creates a texture from an image.
    virtual Result<Texture> CreateTexture(const Image& image) = 0;

    /// @brief Creates a 1x1 texture from a color.
    virtual Result<Texture> CreateTexture(const RgbaColorf& color) = 0;

    /// @brief Destroys a texture.
    virtual Result<void> DestroyTexture(Texture& texture) = 0;

    /// @brief Creates a vertex shader from the given specification.
    virtual Result<VertexShader> CreateVertexShader(const VertexShaderSpec& shaderSpec) = 0;

    /// @brief Destroys a vertex shader.
    virtual Result<void> DestroyVertexShader(VertexShader& shader) = 0;

    /// @brief Creates a fragment shader from the given specification.
    virtual Result<FragmentShader> CreateFragmentShader(const FragmentShaderSpec& shaderSpec) = 0;

    /// @brief Destroys a fragment shader.
    virtual Result<void> DestroyFragmentShader(FragmentShader& shader) = 0;

    virtual Result<RenderGraph*> CreateRenderGraph() = 0;

    virtual void DestroyRenderGraph(RenderGraph* renderGraph) = 0;
};