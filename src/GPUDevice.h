#pragma once

#include "RefCount.h"
#include "Error.h"
#include "VecMath.h"
#include "Vertex.h"
#include "Material.h"
#include <span>
#include <tuple>
#include <variant>

class Model;
class RenderGraph;
class ModelSpec;
class Image;

class TextureSpec
{
public:

    std::variant<std::string, RefPtr<Image>, RgbaColorf> Source;
};

class ShaderSpec
{
public:

    //FIXME(KB) - add support for embedded source code.
    //FIXME(KB) - add support for resource paths.
    std::variant<std::string> Source;

    const unsigned NumUniformBuffers{ 0 };
};

class GpuBuffer
{
public:

    GpuBuffer() {}

    virtual ~GpuBuffer() = 0 {}

    IMPLEMENT_REFCOUNT(GpuBuffer);
};

class GpuVertexBuffer
{
public:

    GpuVertexBuffer() = delete;

    GpuVertexBuffer(RefPtr<GpuBuffer> gpuBuffer, const uint32_t offset)
        : GpuBuffer(gpuBuffer)
        , Offset(offset)
    {
    }

    RefPtr<GpuBuffer> const GpuBuffer;
    const uint32_t Offset;
};

class GpuIndexBuffer
{
public:

    GpuIndexBuffer() = delete;

    GpuIndexBuffer(RefPtr<GpuBuffer> gpuBuffer, const uint32_t offset)
        : GpuBuffer(gpuBuffer)
        , Offset(offset)
    {
    }

    RefPtr<GpuBuffer> const GpuBuffer;
    const uint32_t Offset;
};

class GpuVertexShader
{
public:

    GpuVertexShader() {}
    
    virtual ~GpuVertexShader() = 0 {}

    IMPLEMENT_REFCOUNT(GpuVertexShader);
};

class GpuFragmentShader
{
public:

    GpuFragmentShader() {}
    
    virtual ~GpuFragmentShader() = 0 {}

    IMPLEMENT_REFCOUNT(GpuFragmentShader);
};

class GpuTexture
{
public:

    GpuTexture() {}
    
    virtual ~GpuTexture() = 0 {}

    IMPLEMENT_REFCOUNT(GpuTexture);
};

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

    /// @brief Creates vertex and index buffers from the given vertices and indices.
    /// Internally combines multiple source buffers into a single GPU buffer.
    /// Pass empty spans to indicate no data for that source buffer.
    /// The number of vertex and index buffers must match, even if some are empty.
    virtual Result<std::tuple<GpuVertexBuffer, GpuIndexBuffer>> CreateBuffers(
        const std::span<std::span<const Vertex>>& vertices,
        const std::span<std::span<const uint32_t>>& indices) = 0;

    /// @brief Creates a texture from the given specification.
    virtual Result<RefPtr<GpuTexture>> CreateTexture(const TextureSpec& textureSpec) = 0;

    /// @brief Creates a vertex shader from the given specification.
    virtual Result<RefPtr<GpuVertexShader>> CreateVertexShader(const ShaderSpec& shaderSpec) = 0;

    /// @brief Creates a fragment shader from the given specification.
    virtual Result<RefPtr<GpuFragmentShader>> CreateFragmentShader(const ShaderSpec& shaderSpec) = 0;

    IMPLEMENT_REFCOUNT(GPUDevice);
};