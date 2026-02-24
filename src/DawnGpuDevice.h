#pragma once

#include "DawnRenderer.h"
#include "GpuDevice.h"
#include "PoolAllocator.h"
#include "Vertex.h"

#include <span>
#include <webgpu/webgpu_cpp.h>

struct SDL_Window;

class DawnGpuVertexBuffer : public GpuVertexBuffer
{
    class DawnSubrange : public Subrange
    {
    public:
        DawnSubrange(DawnGpuVertexBuffer* owner, const uint32_t itemOffset, const uint32_t itemCount)
            : Subrange(owner, itemOffset, itemCount)
        {
        }
    };

public:
    DawnGpuVertexBuffer() = delete;
    DawnGpuVertexBuffer(const DawnGpuVertexBuffer&) = delete;
    DawnGpuVertexBuffer& operator=(const DawnGpuVertexBuffer&) = delete;
    DawnGpuVertexBuffer(DawnGpuVertexBuffer&&) = delete;
    DawnGpuVertexBuffer& operator=(DawnGpuVertexBuffer&&) = delete;

    // wgpu::Buffer is ref counted so nothing to do here.
    ~DawnGpuVertexBuffer() override {};

    wgpu::Buffer GetBuffer() const { return m_Buffer; }

    Subrange GetSubrange(const uint32_t itemOffset, const uint32_t itemCount) override
    {
        eassert(itemOffset + itemCount <= m_ItemCount, "Sub-range out of bounds");
        return DawnSubrange(this, itemOffset, itemCount);
    }

    uint32_t GetVertexCount() const override { return m_ItemCount; }

private:
    friend class DawnGpuDevice;

    explicit DawnGpuVertexBuffer(
        DawnGpuDevice* gpuDevice, wgpu::Buffer buffer, const uint32_t itemCount)
        : m_GpuDevice(gpuDevice),
          m_Buffer(buffer),
          m_ItemCount(itemCount)
    {
    }

    DawnGpuDevice* m_GpuDevice;
    wgpu::Buffer m_Buffer;
    const uint32_t m_ItemCount;
};

class DawnGpuIndexBuffer : public GpuIndexBuffer
{
    class DawnSubrange : public Subrange
    {
    public:
        DawnSubrange(DawnGpuIndexBuffer* owner, const uint32_t itemOffset, const uint32_t itemCount)
            : Subrange(owner, itemOffset, itemCount)
        {
        }
    };

public:
    DawnGpuIndexBuffer() = delete;
    DawnGpuIndexBuffer(const DawnGpuIndexBuffer&) = delete;
    DawnGpuIndexBuffer& operator=(const DawnGpuIndexBuffer&) = delete;
    DawnGpuIndexBuffer(DawnGpuIndexBuffer&&) = delete;
    DawnGpuIndexBuffer& operator=(DawnGpuIndexBuffer&&) = delete;

    // wgpu::Buffer is ref counted so nothing to do here.
    ~DawnGpuIndexBuffer() override {};

    wgpu::Buffer GetBuffer() const { return m_Buffer; }

    Subrange GetSubrange(const uint32_t itemOffset, const uint32_t itemCount) override
    {
        eassert(itemOffset + itemCount <= m_ItemCount, "Sub-range out of bounds");
        return DawnSubrange(this, itemOffset, itemCount);
    }

    uint32_t GetIndexCount() const override { return m_ItemCount; }

private:
    friend class DawnGpuDevice;

    explicit DawnGpuIndexBuffer(
        DawnGpuDevice* gpuDevice, wgpu::Buffer buffer, const uint32_t itemCount)
        : m_GpuDevice(gpuDevice),
          m_Buffer(buffer),
          m_ItemCount(itemCount)
    {
    }

    DawnGpuDevice* m_GpuDevice;
    wgpu::Buffer m_Buffer;
    const uint32_t m_ItemCount;
};

class DawnGpuTexture : public GpuTexture
{
public:
    DawnGpuTexture() = delete;
    DawnGpuTexture(const DawnGpuTexture&) = delete;
    DawnGpuTexture& operator=(const DawnGpuTexture&) = delete;
    DawnGpuTexture(DawnGpuTexture&&) = delete;
    DawnGpuTexture& operator=(DawnGpuTexture&&) = delete;

    // wgpu::Texture is ref counted so nothing to do here.
    ~DawnGpuTexture() override {};

    unsigned GetWidth() const override { return m_Width; }
    unsigned GetHeight() const override { return m_Height; }

    wgpu::Texture GetTexture() const { return m_Texture; }
    wgpu::TextureView GetTextureView() const { return m_TextureView; }
    wgpu::Sampler GetSampler() const { return m_Sampler; }

private:
    friend class DawnGpuDevice;

    DawnGpuTexture(DawnGpuDevice* gpuDevice,
        wgpu::Texture texture,
        wgpu::TextureView textureView,
        wgpu::Sampler sampler,
        const unsigned width,
        const unsigned height)
        : m_GpuDevice(gpuDevice),
          m_Texture(texture),
          m_TextureView(textureView),
          m_Sampler(sampler),
          m_Width(width),
          m_Height(height)
    {
    }

    DawnGpuDevice* m_GpuDevice;
    wgpu::Texture m_Texture;
    wgpu::TextureView m_TextureView;
    wgpu::Sampler m_Sampler;
    unsigned m_Width;
    unsigned m_Height;
};

class DawnGpuMaterial : public GpuMaterial
{
public:

    DawnGpuMaterial() = delete;
    DawnGpuMaterial(const DawnGpuMaterial&) = delete;
    DawnGpuMaterial& operator=(const DawnGpuMaterial&) = delete;
    DawnGpuMaterial(DawnGpuMaterial&&) = delete;
    DawnGpuMaterial& operator=(DawnGpuMaterial&&) = delete;

    ~DawnGpuMaterial() override {};

    GpuTexture* GetBaseTexture() const override { return m_BaseTexture; }
    const RgbaColorf& GetColor() const override { return m_Color; }
    float GetMetalness() const override { return m_Metalness; }
    float GetRoughness() const override { return m_Roughness; }

    wgpu::BindGroup GetBindGroup() const { return m_BindGroup; }

private:
    friend class DawnGpuDevice;

    DawnGpuMaterial(DawnGpuDevice* gpuDevice,
        GpuTexture* baseTexture,
        wgpu::Buffer constantsBuffer,
        wgpu::BindGroup bindGroup,
        const RgbaColorf& color,
        const float metalness,
        const float roughness)
        : m_GpuDevice(gpuDevice),
          m_BaseTexture(baseTexture),
          m_ConstantsBuffer(constantsBuffer),
          m_BindGroup(bindGroup),
          m_Color(color),
          m_Metalness(metalness),
          m_Roughness(roughness)

    {
    }

    DawnGpuDevice* const m_GpuDevice;
    GpuTexture* m_BaseTexture;
    wgpu::Buffer m_ConstantsBuffer;
    wgpu::BindGroup m_BindGroup;
    RgbaColorf m_Color;
    float m_Metalness;
    float m_Roughness;
};

class DawnGpuColorTarget : public GpuColorTarget
{
public:
    DawnGpuColorTarget() = delete;
    DawnGpuColorTarget(const DawnGpuColorTarget&) = delete;
    DawnGpuColorTarget& operator=(const DawnGpuColorTarget&) = delete;
    DawnGpuColorTarget(DawnGpuColorTarget&&) = delete;
    DawnGpuColorTarget& operator=(DawnGpuColorTarget&&) = delete;

    // wgpu::Texture is ref counted so nothing to do here.
    ~DawnGpuColorTarget() override {};

    unsigned GetWidth() const override { return m_Width; }
    unsigned GetHeight() const override { return m_Height; }
    wgpu::TextureFormat GetFormat() const { return m_Format; }

    wgpu::Texture GetTexture() const { return m_Texture; }
    wgpu::TextureView GetTextureView() const { return m_TextureView; }
    wgpu::Sampler GetSampler() const { return m_Sampler; }

private:
    friend class DawnGpuDevice;

    DawnGpuColorTarget(DawnGpuDevice* gpuDevice,
        wgpu::Texture texture,
        wgpu::TextureView textureView,
        wgpu::Sampler sampler,
        const unsigned width,
        const unsigned height,
        wgpu::TextureFormat format)
        : m_GpuDevice(gpuDevice),
          m_Texture(texture),
          m_TextureView(textureView),
          m_Sampler(sampler),
          m_Width(width),
          m_Height(height),
          m_Format(format)
    {
    }

    DawnGpuDevice* m_GpuDevice;
    wgpu::Texture m_Texture;
    wgpu::TextureView m_TextureView;
    wgpu::Sampler m_Sampler;
    unsigned m_Width;
    unsigned m_Height;
    wgpu::TextureFormat m_Format;
};

class DawnGpuDepthTarget : public GpuDepthTarget
{
public:
    DawnGpuDepthTarget() = delete;
    DawnGpuDepthTarget(const DawnGpuDepthTarget&) = delete;
    DawnGpuDepthTarget& operator=(const DawnGpuDepthTarget&) = delete;
    DawnGpuDepthTarget(DawnGpuDepthTarget&&) = delete;
    DawnGpuDepthTarget& operator=(DawnGpuDepthTarget&&) = delete;

    // wgpu::Texture is ref counted so nothing to do here.
    ~DawnGpuDepthTarget() override {};

    unsigned GetWidth() const override { return m_Width; }
    unsigned GetHeight() const override { return m_Height; }
    wgpu::TextureFormat GetFormat() const { return m_Format; }

    wgpu::Texture GetTexture() const { return m_DepthTarget; }
    wgpu::TextureView GetTextureView() const { return m_DepthTargetView; }

private:
    friend class DawnGpuDevice;

    DawnGpuDepthTarget(DawnGpuDevice* gpuDevice,
        wgpu::Texture depthTarget,
        wgpu::TextureView depthTargetView,
        const unsigned width,
        const unsigned height,
        wgpu::TextureFormat format)
        : m_GpuDevice(gpuDevice),
          m_DepthTarget(depthTarget),
          m_DepthTargetView(depthTargetView),
          m_Width(width),
          m_Height(height),
          m_Format(format)
    {
    }

    DawnGpuDevice* m_GpuDevice;
    wgpu::Texture m_DepthTarget;
    wgpu::TextureView m_DepthTargetView;
    unsigned m_Width;
    unsigned m_Height;
    wgpu::TextureFormat m_Format;
};

class DawnGpuVertexShader : public GpuVertexShader
{
public:
    DawnGpuVertexShader() = delete;
    DawnGpuVertexShader(const DawnGpuVertexShader&) = delete;
    DawnGpuVertexShader& operator=(const DawnGpuVertexShader&) = delete;
    DawnGpuVertexShader(DawnGpuVertexShader&&) = delete;
    DawnGpuVertexShader& operator=(DawnGpuVertexShader&&) = delete;

    ~DawnGpuVertexShader() override {};

    wgpu::ShaderModule GetShader() const { return m_Shader; }

private:
    friend class DawnGpuDevice;

    explicit DawnGpuVertexShader(DawnGpuDevice* gpuDevice, wgpu::ShaderModule shader)
        : m_GpuDevice(gpuDevice),
          m_Shader(shader)
    {
    }

    DawnGpuDevice* m_GpuDevice;
    wgpu::ShaderModule m_Shader;
};

class DawnGpuFragmentShader : public GpuFragmentShader
{
public:
    DawnGpuFragmentShader() = delete;
    DawnGpuFragmentShader(const DawnGpuFragmentShader&) = delete;
    DawnGpuFragmentShader& operator=(const DawnGpuFragmentShader&) = delete;
    DawnGpuFragmentShader(DawnGpuFragmentShader&&) = delete;
    DawnGpuFragmentShader& operator=(DawnGpuFragmentShader&&) = delete;

    ~DawnGpuFragmentShader() override {};

    wgpu::ShaderModule GetShader() const { return m_Shader; }

private:
    friend class DawnGpuDevice;
    explicit DawnGpuFragmentShader(DawnGpuDevice* gpuDevice, wgpu::ShaderModule shader)
        : m_GpuDevice(gpuDevice),
          m_Shader(shader)
    {
    }

    DawnGpuDevice* m_GpuDevice;
    wgpu::ShaderModule m_Shader;
};

/// @brief Dawn GPU Device implementation.
class DawnGpuDevice : public GpuDevice
{
public:
    static Result<GpuDevice*> Create(SDL_Window* window);

    static void Destroy(GpuDevice* device);

    DawnGpuDevice() = delete;
    DawnGpuDevice(const DawnGpuDevice&) = delete;
    DawnGpuDevice& operator=(const DawnGpuDevice&) = delete;
    DawnGpuDevice(DawnGpuDevice&&) = delete;
    DawnGpuDevice& operator=(DawnGpuDevice&&) = delete;

    ~DawnGpuDevice() override;

    /// @brief Gets the renderable extent of the device.
    Extent GetScreenBounds() const override;

    Result<GpuVertexBuffer*> CreateVertexBuffer(const std::span<const Vertex>& vertices) override;

    Result<GpuVertexBuffer*> CreateVertexBuffer(
        const std::span<std::span<const Vertex>>& vertices) override;

    Result<void> DestroyVertexBuffer(GpuVertexBuffer* buffer) override;

    Result<GpuIndexBuffer*> CreateIndexBuffer(const std::span<const VertexIndex>& indices) override;

    Result<GpuIndexBuffer*> CreateIndexBuffer(
        const std::span<std::span<const VertexIndex>>& indices) override;

    Result<void> DestroyIndexBuffer(GpuIndexBuffer* buffer) override;

    Result<GpuTexture*> CreateTexture(const unsigned width,
        const unsigned height,
        const uint8_t* pixels,
        const unsigned rowStride,
        const imstring& name) override;

    Result<GpuTexture*> CreateTexture(const RgbaColorf& color, const imstring& name) override;

    Result<void> DestroyTexture(GpuTexture* texture) override;

    Result<GpuColorTarget*> CreateColorTarget(
        const unsigned width, const unsigned height, const imstring& name) override;

    Result<void> DestroyColorTarget(GpuColorTarget* colorTarget) override;

    Result<GpuDepthTarget*> CreateDepthTarget(const unsigned width,
        const unsigned height,
        const imstring& name) override;

    Result<void> DestroyDepthTarget(GpuDepthTarget* depthTarget) override;

    Result<GpuVertexShader*> CreateVertexShader(const std::span<const uint8_t>& shaderCode) override;

    Result<void> DestroyVertexShader(GpuVertexShader* shader) override;

    Result<GpuFragmentShader*> CreateFragmentShader(const std::span<const uint8_t>& shaderCode) override;

    Result<void> DestroyFragmentShader(GpuFragmentShader* shader) override;

    Result<GpuMaterial*> CreateMaterial(const MaterialConstants& constants,
        GpuTexture* baseTexture) override;

    Result<void> DestroyMaterial(GpuMaterial* material) override;

    Result<Renderer*> CreateRenderer() override;

    void DestroyRenderer(Renderer* renderer) override;

    wgpu::TextureFormat GetSwapChainFormat() const;

    SDL_Window* const Window;
    wgpu::Instance const Instance;
    wgpu::Adapter const Adapter;
    wgpu::Device const Device;
    wgpu::Surface const Surface;

private:
    DawnGpuDevice(SDL_Window* window,
        wgpu::Instance instance,
        wgpu::Adapter adapter,
        wgpu::Device device,
        wgpu::Surface surface);

    Result<wgpu::Sampler> GetDefaultSampler();

    /// @brief Gets the default bind group layout for fragment shaders.
    Result<wgpu::BindGroupLayout> GetFsBindGroupLayout();

    /// @brief Default sampler used for all textures.
    wgpu::Sampler m_Sampler;

    /// @brief Default bind group layout for fragment shaders.
    wgpu::BindGroupLayout m_FsBindGroupLayout;

    union GpuResource
    {
        GpuResource() {}
        ~GpuResource() {}

        DawnGpuVertexBuffer VertexBuffer;
        DawnGpuIndexBuffer IndexBuffer;
        DawnGpuTexture Texture;
        DawnGpuMaterial Material;
        DawnGpuColorTarget ColorTarget;
        DawnGpuDepthTarget DepthTarget;
        DawnGpuVertexShader VertexShader;
        DawnGpuFragmentShader FragmentShader;
    };

    PoolAllocator<GpuResource, 256> m_ResourceAllocator;

    PoolAllocator<DawnRenderer, 4> m_RendererAllocator;

    wgpu::TextureFormat m_SwapChainFormat{ wgpu::TextureFormat::Undefined };
};