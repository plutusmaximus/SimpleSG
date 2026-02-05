#pragma once

#include "RenderGraph.h"
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
class VertexShaderSpec;
class FragmentShaderSpec;

/// @brief GPU representation of a vertex buffer.
class GpuVertexBuffer
{
public:
    virtual ~GpuVertexBuffer() = 0 {}
protected:
    GpuVertexBuffer() {}
};

/// @brief GPU representation of an index buffer.
class GpuIndexBuffer
{
public:
    virtual ~GpuIndexBuffer() = 0 {}
protected:
    GpuIndexBuffer() {}
};

/// @brief GPU representation of a vertex shader.
class GpuVertexShader
{
public:
    virtual ~GpuVertexShader() = 0 {}
protected:
    GpuVertexShader() {}
};

/// @brief GPU representation of a fragment shader.
class GpuFragmentShader
{
public:
    virtual ~GpuFragmentShader() = 0 {}
protected:
    GpuFragmentShader() {}
};

/// @brief GPU representation of a texture.
class GpuTexture
{
public:
    virtual ~GpuTexture() = 0 {}
protected:
    GpuTexture() {}
};

/// @brief API representation of a vertex buffer.
/// Contains a reference to the underlying GPU buffer and metadata about the buffer's layout.
class VertexBuffer
{
public:

    class Subrange
    {
        friend class VertexBuffer;
    public:

        Subrange() = default;

        Subrange(const Subrange&) = delete;
        Subrange& operator=(const Subrange&) = delete;
        Subrange(Subrange&& other)
        {
            m_Owner = other.m_Owner;
            m_ByteOffset = other.m_ByteOffset;
            m_ItemCount = other.m_ItemCount;

            other.m_Owner = nullptr;
            other.m_ByteOffset = 0;
            other.m_ItemCount = 0;
        }

        Subrange& operator=(Subrange&& other)
        {
            m_Owner = other.m_Owner;
            m_ByteOffset = other.m_ByteOffset;
            m_ItemCount = other.m_ItemCount;

            other.m_Owner = nullptr;
            other.m_ByteOffset = 0;
            other.m_ItemCount = 0;

            return *this;
        }

        bool IsValid() const { return m_Owner != nullptr; }

        template<typename T>
        T* Get()
        {
            eassert(IsValid(), "Invalid subrange");
            return static_cast<T*>(m_Owner);
        }

        template<typename T>
        const T* Get() const
        {
            eassert(IsValid(), "Invalid subrange");
            return static_cast<const T*>(m_Owner);
        }

        /// @brief Offset in bytes of first item in buffer.
        uint32_t GetByteOffset() const { return m_ByteOffset; }

        /// @brief Number of items in buffer.
        uint32_t GetItemCount() const { return m_ItemCount; }

    private:

        Subrange(GpuVertexBuffer* owner, const uint32_t itemOffset, const uint32_t itemCount)
            : m_Owner(owner), m_ByteOffset(itemOffset * sizeof(Vertex)), m_ItemCount(itemCount)
        {
        }

        // This pointer is borrowed from the parent VertexBuffer.
        // It must not outlive its parent.
        GpuVertexBuffer* m_Owner{ nullptr };
        uint32_t m_ByteOffset{ 0 };
        uint32_t m_ItemCount{ 0 };
    };

    VertexBuffer() = default;
    VertexBuffer(std::unique_ptr<GpuVertexBuffer>&& buffer, const uint32_t itemCount)
        : m_Buffer(std::move(buffer)),
          m_ItemCount(itemCount)
    {
    }

    VertexBuffer(const VertexBuffer&) = delete;
    VertexBuffer& operator=(const VertexBuffer&) = delete;
    VertexBuffer(VertexBuffer&& other) = default;
    VertexBuffer& operator=(VertexBuffer&& other) = default;

    /// @brief Retrieves a sub-range buffer from this buffer.
    Result<Subrange> GetSubRange(const uint32_t itemOffset, const uint32_t itemCount)
    {
        expect(IsValid(), "Invalid buffer");
        expect(itemOffset + itemCount <= m_ItemCount, "Sub-range out of bounds");

        return Subrange(m_Buffer.get(), itemOffset, itemCount);
    }

    GpuVertexBuffer* Get() { return m_Buffer.get(); }
    const GpuVertexBuffer* Get() const { return m_Buffer.get(); }

    bool IsValid() const { return m_Buffer != nullptr; }

    /// @brief Number of items in buffer.
    uint32_t GetItemCount() const { return m_ItemCount; }

private:
    std::unique_ptr<GpuVertexBuffer> m_Buffer{ nullptr };

    uint32_t m_ItemCount{ 0 };
};

/// @brief API representation of an index buffer.
/// Contains a reference to the underlying GPU buffer and metadata about the buffer's layout.
class IndexBuffer
{
public:

    class Subrange
    {
        friend class IndexBuffer;
    public:

        Subrange() = default;

        Subrange(const Subrange&) = delete;
        Subrange& operator=(const Subrange&) = delete;
        Subrange(Subrange&& other)
        {
            m_Owner = other.m_Owner;
            m_ByteOffset = other.m_ByteOffset;
            m_ItemCount = other.m_ItemCount;

            other.m_Owner = nullptr;
            other.m_ByteOffset = 0;
            other.m_ItemCount = 0;
        }

        Subrange& operator=(Subrange&& other)
        {
            m_Owner = other.m_Owner;
            m_ByteOffset = other.m_ByteOffset;
            m_ItemCount = other.m_ItemCount;

            other.m_Owner = nullptr;
            other.m_ByteOffset = 0;
            other.m_ItemCount = 0;

            return *this;
        }

        bool IsValid() const { return m_Owner != nullptr; }

        template<typename T>
        T* Get()
        {
            eassert(IsValid(), "Invalid subrange");
            return static_cast<T*>(m_Owner);
        }

        template<typename T>
        const T* Get() const
        {
            eassert(IsValid(), "Invalid subrange");
            return static_cast<const T*>(m_Owner);
        }

        /// @brief Offset in bytes of first item in buffer.
        uint32_t GetByteOffset() const { return m_ByteOffset; }

        /// @brief Number of items in buffer.
        uint32_t GetItemCount() const { return m_ItemCount; }

    private:

        Subrange(GpuIndexBuffer* owner, const uint32_t itemOffset, const uint32_t itemCount)
            : m_Owner(owner), m_ByteOffset(itemOffset * sizeof(VertexIndex)), m_ItemCount(itemCount)
        {
        }

        // This pointer is borrowed from the parent VertexBuffer.
        // It must not outlive its parent.
        GpuIndexBuffer* m_Owner{ nullptr };
        uint32_t m_ByteOffset{ 0 };
        uint32_t m_ItemCount{ 0 };
    };

    IndexBuffer() = default;
    IndexBuffer(std::unique_ptr<GpuIndexBuffer>&& buffer, const uint32_t itemCount)
        : m_Buffer(std::move(buffer)),
          m_ItemCount(itemCount)
    {
    }

    IndexBuffer(const IndexBuffer&) = delete;
    IndexBuffer& operator=(const IndexBuffer&) = delete;
    IndexBuffer(IndexBuffer&& other) = default;
    IndexBuffer& operator=(IndexBuffer&& other) = default;

    /// @brief Retrieves a sub-range buffer from this buffer.
    Result<Subrange> GetSubRange(const uint32_t itemOffset, const uint32_t itemCount)
    {
        expect(IsValid(), "Invalid buffer");
        expect(itemOffset + itemCount <= m_ItemCount, "Sub-range out of bounds");

        return Subrange(m_Buffer.get(), itemOffset, itemCount);
    }

    GpuIndexBuffer* Get() { return m_Buffer.get(); }
    const GpuIndexBuffer* Get() const { return m_Buffer.get(); }

    bool IsValid() const { return m_Buffer != nullptr; }

    uint32_t GetItemCount() const { return m_ItemCount; }

private:
    std::unique_ptr<GpuIndexBuffer> m_Buffer{ nullptr };

    uint32_t m_ItemCount{ 0 };
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
    T* Get()
    {
        return static_cast<T*>(m_Shader);
    }

    template<typename T>
    const T* Get() const
    {
        return static_cast<const T*>(m_Shader);
    }

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
    T* Get()
    {
        return static_cast<T*>(m_Shader);
    }

    template<typename T>
    const T* Get() const
    {
        return static_cast<const T*>(m_Shader);
    }

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
    T* Get()
    {
        return static_cast<T*>(m_Texture);
    }

    template<typename T>
    const T* Get() const
    {
        return static_cast<const T*>(m_Texture);
    }

    bool IsValid() const { return m_Texture != nullptr; }

private:
    GpuTexture* m_Texture{ nullptr };
};

/// @brief Abstract base class for GPU device implementation.
class GpuDevice
{
public:
    virtual ~GpuDevice() = 0 {};

    /// @brief Gets the renderable extent of the device.
    virtual Extent GetExtent() const = 0;

    /// @brief Creates a vertex buffer from the given vertices.
    virtual Result<VertexBuffer> CreateVertexBuffer(const std::span<const Vertex>& vertices) = 0;

    /// @brief Creates a vertex buffer from multiple spans of vertices.
    virtual Result<VertexBuffer> CreateVertexBuffer(
        const std::span<std::span<const Vertex>>& vertices) = 0;

    /// @brief Creates an index buffer from the given indices.
    virtual Result<IndexBuffer> CreateIndexBuffer(const std::span<const VertexIndex>& indices) = 0;

    /// @brief Creates an index buffer from multiple spans of indices.
    virtual Result<IndexBuffer> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) = 0;

    /// @brief Creates a texture from raw pixel data.
    /// Pixels are expected to be in RGBA8 format.
    /// rowStride is the number of bytes between the start of each row.
    /// rowStride must be at least width * 4.
    virtual Result<Texture> CreateTexture(const unsigned width,
        const unsigned height,
        const uint8_t* pixels,
        const unsigned rowStride,
        const imstring& name) = 0;

    /// @brief Creates a 1x1 texture from a color.
    virtual Result<Texture> CreateTexture(const RgbaColorf& color, const imstring& name) = 0;

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