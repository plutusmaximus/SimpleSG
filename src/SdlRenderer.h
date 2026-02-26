#pragma once

#include "Renderer.h"
#include "Model.h"

#include <map>

class GpuTexture;
struct SDL_GPUBuffer;
struct SDL_GPUCommandBuffer;
struct SDL_GPUGraphicsPipeline;
struct SDL_GPURenderPass;
struct SDL_GPUShader;
struct SDL_GPUTexture;
struct SDL_GPUTransferBuffer;
class SdlGpuDevice;

class SdlRenderer : public Renderer
{
public:

    SdlRenderer() = delete;
    SdlRenderer(const SdlRenderer&) = delete;
    SdlRenderer& operator=(const SdlRenderer&) = delete;
    SdlRenderer(SdlRenderer&&) = delete;
    SdlRenderer& operator=(SdlRenderer&&) = delete;

    ~SdlRenderer() override;

    void AddModel(const Mat44f& worldTransform, const Model* model) override;

    Result<void> Render(
        const Mat44f& camera, const Mat44f& projection, RenderCompositor* compositor) override;

private:

    friend class SdlGpuDevice;

    explicit SdlRenderer(SdlGpuDevice* gpuDevice);

    Result<SDL_GPURenderPass*> BeginRenderPass(SDL_GPUCommandBuffer* cmdBuf);

    struct XformMesh
    {
        const Mat44f WorldTransform;
        const Model* Model;
        const Mesh& MeshInstance;
    };

    using MeshGroup = std::vector<XformMesh>;
    using MeshGroupCollection = std::map<MaterialId, MeshGroup>;

    struct State
    {
        void Clear()
        {
            m_OpaqueMeshGroups.clear();
            m_TranslucentMeshGroups.clear();
            m_MeshCount = 0;
        }

        MeshGroupCollection m_TranslucentMeshGroups;

        MeshGroupCollection m_OpaqueMeshGroups;

        size_t m_MeshCount = 0;
    };

    void SwapStates();

    /// @brief Copy the color target to the swapchain texture.
    Result<void> CopyColorTargetToSwapchain(SDL_GPUCommandBuffer* cmdBuf, SDL_GPUTexture* target);

    Result<SDL_GPUShader*> GetColorVertexShader();
    Result<SDL_GPUShader*> GetColorFragmentShader();
    Result<SDL_GPUGraphicsPipeline*> GetColorPipeline();

    Result<SDL_GPUShader*> GetCopyColorTargetVertexShader();
    Result<SDL_GPUShader*> GetCopyColorTargetFragmentShader();
    Result<SDL_GPUGraphicsPipeline*> GetCopyColorTargetPipeline();

    struct ShaderCreateInfo
    {
        const char* path;
        uint32_t numStorageBuffers;
        uint32_t numUniformBuffers;
    };

    Result<SDL_GPUShader*> CreateVertexShader(const ShaderCreateInfo& createInfo);
    Result<SDL_GPUShader*> CreateFragmentShader(const ShaderCreateInfo& createInfo);

    Result<void> UpdateXformBuffer(
        SDL_GPUCommandBuffer* cmdBuf, const Mat44f& camera, const Mat44f& projection);

    /// Get or create the default texture.
    /// The default texture is used when a material does not have a base texture.
    Result<GpuTexture*> GetDefaultBaseTexture();

    SdlGpuDevice* const m_GpuDevice;
    GpuColorTarget* m_ColorTarget{ nullptr };
    GpuDepthTarget* m_DepthTarget{ nullptr };

    State m_State[2];
    State* m_CurrentState = &m_State[0];
    GpuTexture* m_DefaultBaseTexture{nullptr};

    /// These are used for rendering to the color target texture.
    SDL_GPUShader* m_ColorVertexShader{ nullptr };
    SDL_GPUShader* m_ColorFragmentShader{ nullptr };
    SDL_GPUGraphicsPipeline* m_ColorPipeline{ nullptr };

    /// These are used for copying the color target to the swapchain texture.
    SDL_GPUShader* m_CopyTextureVertexShader{ nullptr };
    SDL_GPUShader* m_CopyTextureFragmentShader{ nullptr };
    SDL_GPUGraphicsPipeline* m_CopyTexturePipeline{ nullptr };

    size_t m_SizeofTransformBuffer{0};
    SDL_GPUBuffer* m_WorldAndProjBuf{nullptr};  // Transform buffer for world and projection matrices
    SDL_GPUTransferBuffer* m_WorldAndProjXferBuf{nullptr};
};