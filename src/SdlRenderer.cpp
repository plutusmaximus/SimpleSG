#define _CRT_SECURE_NO_WARNINGS

#define __LOGGER_NAME__ "SDL "

#include "SdlRenderer.h"

#include "Logging.h"

#include "Result.h"
#include "scope_exit.h"

#include "SdlGpuDevice.h"
#include "SdlRenderCompositor.h"
#include "PerfMetrics.h"

#include <SDL3/SDL_gpu.h>

#include <cstdio>

static constexpr const char* COMPOSITE_COLOR_TARGET_VS = "shaders/Debug/FullScreenTriangle.vs.spv";
static constexpr const char* COMPOSITE_COLOR_TARGET_FS = "shaders/Debug/FullScreenTriangle.ps.spv";

static constexpr const char* COLOR_PIPELINE_VS = "shaders/Debug/VertexShader.vs.spv";
static constexpr const char* COLOR_PIPELINE_FS = "shaders/Debug/FragmentShader.ps.spv";

class XFormBuffer
{
public:

    Mat44f ModelXform;
    Mat44f ModelViewProjXform;

};

SdlRenderer::SdlRenderer(SdlGpuDevice* gpuDevice)
    : m_GpuDevice(gpuDevice)
{
}

SdlRenderer::~SdlRenderer()
{
    if(m_DefaultBaseTexture)
    {
        auto result = m_GpuDevice->DestroyTexture(m_DefaultBaseTexture);
        if(!result)
        {
            logError("Failed to destroy default base texture: {}", result.error());
        }

        m_DefaultBaseTexture = nullptr;
    }

    if(m_ColorTarget)
    {
        auto result = m_GpuDevice->DestroyColorTarget(m_ColorTarget);
        if(!result)
        {
            logError("Failed to destroy default color target: {}", result.error());
        }

        m_ColorTarget = nullptr;
    }

    if(m_DepthTarget)
    {
        auto result = m_GpuDevice->DestroyDepthTarget(m_DepthTarget);
        if(!result)
        {
            logError("Failed to destroy default depth target: {}", result.error());
        }

        m_DepthTarget = nullptr;
    }

    if(m_ColorVertexShader)
    {
        SDL_ReleaseGPUShader(m_GpuDevice->Device, m_ColorVertexShader);

        m_ColorVertexShader = nullptr;
    }

    if(m_ColorFragmentShader)
    {
        SDL_ReleaseGPUShader(m_GpuDevice->Device, m_ColorFragmentShader);

        m_ColorFragmentShader = nullptr;
    }

    if(m_ColorPipeline)
    {
        SDL_ReleaseGPUGraphicsPipeline(m_GpuDevice->Device, m_ColorPipeline);

        m_ColorPipeline = nullptr;
    }

    if(m_CopyTextureVertexShader)
    {
        SDL_ReleaseGPUShader(m_GpuDevice->Device, m_CopyTextureVertexShader);

        m_CopyTextureVertexShader = nullptr;
    }

    if(m_CopyTextureFragmentShader)
    {
        SDL_ReleaseGPUShader(m_GpuDevice->Device, m_CopyTextureFragmentShader);

        m_CopyTextureFragmentShader = nullptr;
    }

    if(m_CopyTextureFragmentShader)
    {
        SDL_ReleaseGPUShader(m_GpuDevice->Device, m_CopyTextureFragmentShader);

        m_CopyTextureFragmentShader = nullptr;
    }

    if(m_CopyTexturePipeline)
    {
        SDL_ReleaseGPUGraphicsPipeline(m_GpuDevice->Device, m_CopyTexturePipeline);
        m_CopyTexturePipeline = nullptr;
    }

    if(m_WorldAndProjBuf)
    {
        SDL_ReleaseGPUBuffer(m_GpuDevice->Device, m_WorldAndProjBuf);
        m_WorldAndProjBuf = nullptr;
    }

    if(m_WorldAndProjXferBuf)
    {
        SDL_ReleaseGPUTransferBuffer(m_GpuDevice->Device, m_WorldAndProjXferBuf);
        m_WorldAndProjXferBuf = nullptr;
    }
}

void
SdlRenderer::AddModel(const Mat44f& worldTransform, const Model* model)
{
    if(!everify(model, "Model pointer is null"))
    {
        return;
    }

    const auto& meshes = model->GetMeshes();
    const auto& meshInstances = model->GetMeshInstances();
    const auto& transformNodes = model->GetTransformNodes();

    std::vector<Mat44f> worldXForms;
    worldXForms.reserve(transformNodes.size());

    // Precompute world transforms for all nodes
    for(const auto& node : transformNodes)
    {
        if(node.ParentIndex >= 0)
        {
            worldXForms.emplace_back(
                worldXForms[node.ParentIndex].Mul(node.Transform));
        }
        else
        {
            worldXForms.emplace_back(worldTransform.Mul(node.Transform));
        }
    }

    for (const auto& meshInstance : meshInstances)
    {
        const Mesh& mesh = meshes[meshInstance.MeshIndex];
        const Material& mtl = mesh.GetMaterial();

        // Determine mesh group based on material properties

        MeshGroup* meshGrp;

        const MaterialKey& key = mtl.GetKey();

        if(key.Flags & MaterialFlags::Translucent)
        {
            meshGrp = &m_CurrentState->m_TranslucentMeshGroups[key.Id];
        }
        else
        {
            meshGrp = &m_CurrentState->m_OpaqueMeshGroups[key.Id];
        }

        XformMesh xformMesh
        {
            .WorldTransform = worldXForms[meshInstance.NodeIndex],
            .Model = model,
            .MeshInstance = mesh
        };

        meshGrp->emplace_back(xformMesh);
        ++m_CurrentState->m_MeshCount;
    }
}

Result<void>
SdlRenderer::Render(const Mat44f& camera, const Mat44f& projection, RenderCompositor* compositor)
{
    static PerfTimer renderTimer("Renderer.Render");
    auto scopedRenderTimer = renderTimer.StartScoped();

    SdlRenderCompositor* sdlCompositor = static_cast<SdlRenderCompositor*>(compositor);

    SDL_GPUCommandBuffer* cmdBuf = sdlCompositor->GetCommandBuffer();
    if(!cmdBuf)
    {
        // No command buffer - likely window minimized.
        // This is not an error.
        return Result<void>::Success;
    }

    static PerfTimer updateXformTimer("Renderer.Render.UpdateXformBuffer");
    {
        auto scopedTimer = updateXformTimer.StartScoped();

        auto updateXformBufResult = UpdateXformBuffer(cmdBuf, camera, projection);
        expect(updateXformBufResult, updateXformBufResult.error());
    }

    SDL_GPURenderPass* renderPass = nullptr;
    static PerfTimer beginRenderPassTimer("Renderer.Render.BeginRenderPass");
    {
        auto scopedBeginRenderPassTimer = beginRenderPassTimer.StartScoped();
        auto renderPassResult = BeginRenderPass(cmdBuf);
        expect(renderPassResult, renderPassResult.error());

        renderPass = renderPassResult.value();
    }

    if(!renderPass)
    {
        //No render pass - likely window minimized.
        //This is not an error.
        return Result<void>::Success;
    }

    auto cleanupRenderPass = scope_exit([&]()
    {
        SDL_EndGPURenderPass(renderPass);
    });

    SDL_BindGPUVertexStorageBuffers(renderPass, 0, &m_WorldAndProjBuf, 1);

    static PerfTimer setPipelineTimer("Renderer.Render.SetPipeline");
    {
        auto scopedTimer = setPipelineTimer.StartScoped();
        auto pipelineResult = GetColorPipeline();
        expect(pipelineResult, pipelineResult.error());
        auto pipeline = pipelineResult.value();
        SDL_BindGPUGraphicsPipeline(renderPass, pipeline);
    }

    static PerfTimer setVsBindGroupTimer("Renderer.Render.SetVsBindGroup");
    {
        auto scopedTimer = setVsBindGroupTimer.StartScoped();
        SDL_GPUBufferBinding bufferBinding//
        {
            .buffer = m_WorldAndProjBuf,
            .offset = 0,
        };
        SDL_BindGPUVertexBuffers(renderPass, 0, &bufferBinding, 1);
    }

    // Render opaque meshes first
    const MeshGroupCollection* meshGroups[] =
    {
        &m_CurrentState->m_OpaqueMeshGroups,
        &m_CurrentState->m_TranslucentMeshGroups
    };

    static PerfTimer drawTimer("Renderer.Render.Draw");
    drawTimer.Start();

    const SdlGpuVertexBuffer* lastVb = nullptr;
    const SdlGpuIndexBuffer* lastIb = nullptr;

    unsigned meshCount = 0;

    for(const auto meshGrpPtr : meshGroups)
    {
        for (auto& [mtlId, xmeshes] : *meshGrpPtr)
        {
            GpuMaterial* gpuMtl = xmeshes[0].MeshInstance.GetGpuMaterial();

            static PerfTimer writeMaterialTimer("Renderer.Render.Draw.WriteMaterialBuffer");
            {
                auto scopedTimer = writeMaterialTimer.StartScoped();

                SDL_PushGPUFragmentUniformData(cmdBuf, 0, &gpuMtl->GetConstants(), sizeof(MaterialConstants));
            }

            // Bind texture and sampler

            GpuTexture* baseTexture = gpuMtl->GetBaseTexture();

            SDL_GPUTextureSamplerBinding samplerBinding
            {
                .texture = static_cast<SdlGpuTexture*>(baseTexture)->GetTexture(),
                .sampler = static_cast<SdlGpuTexture*>(baseTexture)->GetSampler()
            };
            static PerfTimer fsBindingTimer("Renderer.Render.Draw.FsBindings");
            {
                auto scopedTimer = fsBindingTimer.StartScoped();
                SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);
            }

            for (auto& xmesh : xmeshes)
            {
                const Mesh& mesh = xmesh.MeshInstance;

                const auto& vbSubrange = mesh.GetVertexBuffer();
                const auto& ibSubrange = mesh.GetIndexBuffer();

                auto vb = static_cast<const SdlGpuVertexBuffer*>(vbSubrange.GetBuffer());
                auto ib = static_cast<const SdlGpuIndexBuffer*>(ibSubrange.GetBuffer());

                static_assert(VERTEX_INDEX_BITS == 32 || VERTEX_INDEX_BITS == 16);

                constexpr SDL_GPUIndexElementSize idxElSize =
                    (VERTEX_INDEX_BITS == 32)
                    ? SDL_GPU_INDEXELEMENTSIZE_32BIT
                    : SDL_GPU_INDEXELEMENTSIZE_16BIT;

                static PerfTimer setBuffersTimer("Renderer.Render.Draw.SetBuffers");
                if(lastVb != vb || lastIb != ib)
                {
                    auto scopedTimer = setBuffersTimer.StartScoped();

                    SDL_GPUBufferBinding vertexBufferBinding//
                    {
                        .buffer = vb->GetBuffer(),
                        .offset = 0,
                    };

                    SDL_GPUBufferBinding indexBufferBinding//
                    {
                        .buffer = ib->GetBuffer(),
                        .offset = 0,
                    };

                    SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);
                    SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, idxElSize);
                    lastVb = vb;
                    lastIb = ib;
                }

                static PerfTimer drawIndexedTimer("Renderer.Render.Draw.DrawIndexed");
                {
                    auto scopedTimer = drawIndexedTimer.StartScoped();
                    SDL_DrawGPUIndexedPrimitives(renderPass,
                        mesh.GetIndexCount(),
                        1,
                        ibSubrange.GetIndexOffset(),
                        vbSubrange.GetVertexOffset(),
                        meshCount);
                }

                ++meshCount;
            }
        }
    }

    drawTimer.Stop();

    SDL_EndGPURenderPass(renderPass);

    cleanupRenderPass.release();

    static PerfTimer resolveTimer("Renderer.Render.Resolve");
    resolveTimer.Start();

    static PerfTimer copyTimer("Renderer.Render.Resolve.CopyColorTarget");
    {
        auto scopedTimer = copyTimer.StartScoped();
        auto copyResult = CopyColorTargetToSwapchain(cmdBuf, sdlCompositor->GetTarget());
        expect(copyResult, copyResult.error());
    }

    SwapStates();

    resolveTimer.Stop();

    return Result<void>::Success;
}

//private:

Result<SDL_GPURenderPass*>
SdlRenderer::BeginRenderPass(SDL_GPUCommandBuffer* cmdBuf)
{
    const auto screenBounds = m_GpuDevice->GetScreenBounds();

    const unsigned targetWidth = static_cast<unsigned>(screenBounds.Width);
    const unsigned targetHeight = static_cast<unsigned>(screenBounds.Height);

    if(!m_ColorTarget || m_ColorTarget->GetWidth() != targetWidth ||
        m_ColorTarget->GetHeight() != targetHeight)
    {
        logDebug("Creating new color target for render pass with size {}x{}", targetWidth, targetHeight);

        if(m_ColorTarget)
        {
            auto result = m_GpuDevice->DestroyColorTarget(m_ColorTarget);
            if(!result)
            {
                logError("Failed to destroy default color target: {}", result.error());
            }
            m_ColorTarget = nullptr;
        }

        auto result = m_GpuDevice->CreateColorTarget(targetWidth, targetHeight, "ColorTarget");
        expect(result, result.error());
        m_ColorTarget = result.value();
    }

    if(!m_DepthTarget || m_DepthTarget->GetWidth() != targetWidth ||
        m_DepthTarget->GetHeight() != targetHeight)
    {
        logDebug("Creating new depth target for render pass with size {}x{}", targetWidth, targetHeight);

        if(m_DepthTarget)
        {
            auto result = m_GpuDevice->DestroyDepthTarget(m_DepthTarget);
            if(!result)
            {
                logError("Failed to destroy default depth target: {}", result.error());
            }
            m_DepthTarget = nullptr;
        }

        auto result = m_GpuDevice->CreateDepthTarget(targetWidth, targetHeight, "DepthTarget");
        expect(result, result.error());
        m_DepthTarget = result.value();
    }

    SDL_GPUColorTargetInfo colorTargetInfo
    {
        .texture = static_cast<SdlGpuColorTarget*>(m_ColorTarget)->GetColorTarget(),
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = {0, 0, 0, 0},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE
    };

    static constexpr float CLEAR_DEPTH = 1.0f;

    SDL_GPUDepthStencilTargetInfo depthTargetInfo
    {
        .texture = static_cast<SdlGpuDepthTarget*>(m_DepthTarget)->GetDepthTarget(),
        .clear_depth = CLEAR_DEPTH,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE
    };

    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(
        cmdBuf,
        &colorTargetInfo,
        1,
        &depthTargetInfo);

    if(!renderPass)
    {
        // If we fail to begin the render pass, it's likely because the window is minimized and the
        // swapchain texture is not available. In this case, we can just skip rendering this frame
        // without treating it as an error.
        return nullptr;
    }

    /*const SDL_GPUViewport viewport
    {
        0, 0, static_cast<float>(targetWidth), static_cast<float>(targetHeight), 0, CLEAR_DEPTH
    };
    SDL_SetGPUViewport(renderPass, &viewport);*/

    return renderPass;
}

void
SdlRenderer::SwapStates()
{
    if (m_CurrentState == &m_State[0])
    {
        m_CurrentState = &m_State[1];
    }
    else
    {
        m_CurrentState = &m_State[0];
    }

    m_CurrentState->Clear();
}

Result<void>
SdlRenderer::CopyColorTargetToSwapchain(SDL_GPUCommandBuffer* cmdBuf, SDL_GPUTexture* target)
{
    if(!target)
    {
        // Offscreen rendering - no swapchain texture available. Not an error, just skip copying.
        return Result<void>::Success;
    }

    auto pipelineResult = GetCopyColorTargetPipeline();
    expect(pipelineResult, pipelineResult.error());

    auto pipeline = pipelineResult.value();

    SDL_GPUColorTargetInfo colorTargetInfo
    {
        .texture = target,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = {0, 0, 0, 0},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE
    };

    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(
        cmdBuf,
        &colorTargetInfo,
        1,
        nullptr);

    if(!renderPass)
    {
        // If we fail to begin the render pass, it's likely because the window is minimized and the
        // swapchain texture is not available. In this case, we can just skip rendering this frame
        // without treating it as an error.
        return Result<void>::Success;
    }

    // Bind texture and sampler
    SDL_GPUTextureSamplerBinding samplerBinding
    {
        .texture = static_cast<SdlGpuColorTarget*>(m_ColorTarget)->GetColorTarget(),
        .sampler = static_cast<SdlGpuColorTarget*>(m_ColorTarget)->GetSampler()
    };

    SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);
    SDL_BindGPUGraphicsPipeline(renderPass, pipeline);
    SDL_DrawGPUPrimitives(renderPass, 3, 1, 0, 1);
    SDL_EndGPURenderPass(renderPass);

    return Result<void>::Success;
}

static Result<void>
LoadShaderCode(const char* filePath, std::vector<uint8_t>& outBuffer)
{
    FILE* fp = std::fopen(filePath, "rb");
    expect(fp, "Failed to open shader file: {} ({})", filePath, std::strerror(errno));

    auto cleanupFile = scope_exit([&]() { std::fclose(fp); });

    //Get file size
    if(std::fseek(fp, 0, SEEK_END) != 0)
    {
        return Error("Failed to seek in shader file: {} ({})", filePath, std::strerror(errno));
    }

    long fileSize = std::ftell(fp);
    if(fileSize < 0)
    {
        return Error("Failed to get size of shader file: {} ({})", filePath, std::strerror(errno));
    }
    std::rewind(fp);

    outBuffer.resize(static_cast<size_t>(fileSize));

    expect(std::fread(outBuffer.data(), 1, static_cast<size_t>(fileSize), fp) ==
                static_cast<size_t>(fileSize),
            "Failed to read shader file: {} ({})", filePath, std::strerror(errno));

    return Result<void>::Success;
}

Result<SDL_GPUShader*> SdlRenderer::GetColorVertexShader()
{
    if(!m_ColorVertexShader)
    {
        ShaderCreateInfo createInfo//
        {
            .path = COLOR_PIPELINE_VS,
            .numStorageBuffers = 1,
            .numUniformBuffers = 0
        };
        auto vsResult = CreateVertexShader(createInfo);
        expect(vsResult, vsResult.error());

        m_ColorVertexShader = vsResult.value();
    }

    return m_ColorVertexShader;
}

Result<SDL_GPUShader*> SdlRenderer::GetColorFragmentShader()
{
    if(!m_ColorFragmentShader)
    {
        ShaderCreateInfo createInfo//
        {
            .path = COLOR_PIPELINE_FS,
            .numStorageBuffers = 0,
            .numUniformBuffers = 1
        };
        auto fsResult = CreateFragmentShader(createInfo);
        expect(fsResult, fsResult.error());

        m_ColorFragmentShader = fsResult.value();
    }

    return m_ColorFragmentShader;
}

Result<SDL_GPUGraphicsPipeline*>
SdlRenderer::GetColorPipeline()
{
    if(m_ColorPipeline)
    {
        return m_ColorPipeline;
    }

    if(!everify(m_ColorTarget, "Color target is null"))
    {
        return Error("Color target is null");
    }

    auto vertexShaderResult = GetColorVertexShader();
    expect(vertexShaderResult, vertexShaderResult.error());
    auto vertexShader = vertexShaderResult.value();

    auto fragmentShaderResult = GetColorFragmentShader();
    expect(fragmentShaderResult, fragmentShaderResult.error());
    auto fragmentShader = fragmentShaderResult.value();

    SDL_GPUVertexBufferDescription vertexBufDescriptions[1] = //
        {
            {
                .slot = 0,
                .pitch = sizeof(Vertex),
                .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX,
            },
        };
    SDL_GPUVertexAttribute vertexAttributes[] = //
        {
            {
                //
                .location = 0,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                .offset = offsetof(Vertex, pos),
            },
            {
                //
                .location = 1,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3,
                .offset = offsetof(Vertex, normal),
            },
            { //
                .location = 2,
                .buffer_slot = 0,
                .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2,
                .offset = offsetof(Vertex, uvs[0]),
            },
        };

    SDL_GPUColorTargetDescription colorTargetDesc//
    {
        .format = static_cast<SdlGpuColorTarget*>(m_ColorTarget)->GetFormat(),
        .blend_state =
        {
            .src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA,
            .dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA,
            .color_blend_op = SDL_GPU_BLENDOP_ADD,
            .src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE,
            .dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO,
            .alpha_blend_op = SDL_GPU_BLENDOP_ADD,
            .color_write_mask = SDL_GPU_COLORCOMPONENT_R |
                               SDL_GPU_COLORCOMPONENT_G |
                               SDL_GPU_COLORCOMPONENT_B |
                               SDL_GPU_COLORCOMPONENT_A,
            .enable_blend = true,
            .enable_color_write_mask = false,
        },
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo //
        {
            .vertex_shader = vertexShader,
            .fragment_shader = fragmentShader,
            .vertex_input_state = //
            {
                .vertex_buffer_descriptions = vertexBufDescriptions,
                .num_vertex_buffers = 1,
                .vertex_attributes = vertexAttributes,
                .num_vertex_attributes = std::size(vertexAttributes),
            },
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = //
            {
                .fill_mode = SDL_GPU_FILLMODE_FILL,
                .cull_mode = SDL_GPU_CULLMODE_BACK,
                .front_face = SDL_GPU_FRONTFACE_CLOCKWISE,
                .enable_depth_clip = true,
            },
            .depth_stencil_state = //
            {
                .compare_op = SDL_GPU_COMPAREOP_LESS,
                .enable_depth_test = true,
                .enable_depth_write = true,
            },
            .target_info = //
            {
                .color_target_descriptions = &colorTargetDesc,
                .num_color_targets = 1,
                .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
                .has_depth_stencil_target = true,
            },
        };

    m_ColorPipeline = SDL_CreateGPUGraphicsPipeline(m_GpuDevice->Device, &pipelineCreateInfo);
    expect(m_ColorPipeline, SDL_GetError());

    return m_ColorPipeline;
}

Result<SDL_GPUShader*>
SdlRenderer::GetCopyColorTargetVertexShader()
{
    if(!m_CopyTextureVertexShader)
    {
        ShaderCreateInfo createInfo//
        {
            .path = COMPOSITE_COLOR_TARGET_VS,
            .numStorageBuffers = 0,
            .numUniformBuffers = 0,
        };
        auto vsResult = CreateVertexShader(createInfo);
        expect(vsResult, vsResult.error());

        m_CopyTextureVertexShader = vsResult.value();
    }

    return m_CopyTextureVertexShader;
}

Result<SDL_GPUShader*>
SdlRenderer::GetCopyColorTargetFragmentShader()
{
    if(!m_CopyTextureFragmentShader)
    {
        ShaderCreateInfo createInfo//
        {
            .path = COMPOSITE_COLOR_TARGET_FS,
            .numStorageBuffers = 0,
            .numUniformBuffers = 0,
        };
        auto fsResult = CreateFragmentShader(createInfo);
        expect(fsResult, fsResult.error());

        m_CopyTextureFragmentShader = fsResult.value();
    }

    return m_CopyTextureFragmentShader;
}

Result<SDL_GPUGraphicsPipeline*>
SdlRenderer::GetCopyColorTargetPipeline()
{
    if(m_CopyTexturePipeline)
    {
        return m_CopyTexturePipeline;
    }

    auto vsResult = GetCopyColorTargetVertexShader();
    expect(vsResult, vsResult.error());

    auto fsResult = GetCopyColorTargetFragmentShader();
    expect(fsResult, fsResult.error());

    auto colorTargetFormat = m_GpuDevice->GetSwapChainFormat();

    SDL_GPUColorTargetDescription colorTargetDesc//
    {
        .format = colorTargetFormat,
        .blend_state =
        {
            .enable_blend = false,
            .enable_color_write_mask = false,
        },
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo //
        {
            .vertex_shader = vsResult.value(),
            .fragment_shader = fsResult.value(),
            .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
            .rasterizer_state = //
            {
                .fill_mode = SDL_GPU_FILLMODE_FILL,
                .cull_mode = SDL_GPU_CULLMODE_BACK,
                .front_face = SDL_GPU_FRONTFACE_CLOCKWISE,
                .enable_depth_clip = false,
            },
            .depth_stencil_state = //
            {
                .enable_depth_test = false,
                .enable_depth_write = false,
            },
            .target_info = //
            {
                .color_target_descriptions = &colorTargetDesc,
                .num_color_targets = 1,
                .has_depth_stencil_target = false,
            },
        };

    m_CopyTexturePipeline = SDL_CreateGPUGraphicsPipeline(m_GpuDevice->Device, &pipelineCreateInfo);
    expect(m_CopyTexturePipeline, SDL_GetError());

    return m_CopyTexturePipeline;
}

Result<SDL_GPUShader*>
SdlRenderer::CreateVertexShader(const ShaderCreateInfo& createInfo)
{
    std::vector<uint8_t> shaderCode;
    auto loadResult = LoadShaderCode(createInfo.path, shaderCode);
    expect(loadResult, loadResult.error());

    SDL_GPUShaderCreateInfo shaderCreateInfo
    {
        .code_size = shaderCode.size(),
        .code = shaderCode.data(),
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_VERTEX,
        .num_samplers = 0,
        .num_storage_buffers = createInfo.numStorageBuffers
    };

    SDL_GPUShader* shader = SDL_CreateGPUShader(m_GpuDevice->Device, &shaderCreateInfo);
    expect(shader, SDL_GetError());

    return shader;
}

Result<SDL_GPUShader*>
SdlRenderer::CreateFragmentShader(const ShaderCreateInfo& createInfo)
{
    std::vector<uint8_t> shaderCode;
    auto loadResult = LoadShaderCode(createInfo.path, shaderCode);
    expect(loadResult, loadResult.error());

    SDL_GPUShaderCreateInfo shaderCreateInfo
    {
        .code_size = shaderCode.size(),
        .code = shaderCode.data(),
        .entrypoint = "main",
        .format = SDL_GPU_SHADERFORMAT_SPIRV,
        .stage = SDL_GPU_SHADERSTAGE_FRAGMENT,
        .num_samplers = 1,
        .num_uniform_buffers = createInfo.numUniformBuffers
    };

    SDL_GPUShader* shader = SDL_CreateGPUShader(m_GpuDevice->Device, &shaderCreateInfo);
    expect(shader, SDL_GetError());

    return shader;
}

Result<void>
SdlRenderer::UpdateXformBuffer(
    SDL_GPUCommandBuffer* cmdBuf, const Mat44f& camera, const Mat44f& projection)
{
    // Size of the buffer needed to hold the world and projection matrices for all meshes in the
    // current frame.
    const size_t sizeofTransformBuffer = sizeof(XFormBuffer) * m_CurrentState->m_MeshCount;

    if(!m_WorldAndProjBuf || !m_WorldAndProjXferBuf ||
        m_SizeofTransformBuffer < sizeofTransformBuffer)
    {
        if(m_WorldAndProjBuf)
        {
            SDL_ReleaseGPUBuffer(m_GpuDevice->Device, m_WorldAndProjBuf);
            m_WorldAndProjBuf = nullptr;
        }

        if(m_WorldAndProjXferBuf)
        {
            SDL_ReleaseGPUTransferBuffer(m_GpuDevice->Device, m_WorldAndProjXferBuf);
            m_WorldAndProjXferBuf = nullptr;
        }

        SDL_GPUBufferCreateInfo bufferCreateInfo//
        {
            .usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ,
            .size = static_cast<uint32_t>(sizeofTransformBuffer)
        };

        m_WorldAndProjBuf = SDL_CreateGPUBuffer(m_GpuDevice->Device, &bufferCreateInfo);
        expect(m_WorldAndProjBuf, SDL_GetError());

        SDL_GPUTransferBufferCreateInfo xferBufferCreateInfo //
            {
                .usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD,
                .size = static_cast<uint32_t>(sizeofTransformBuffer),
            };

        m_WorldAndProjXferBuf =
            SDL_CreateGPUTransferBuffer(m_GpuDevice->Device, &xferBufferCreateInfo);
        expect(m_WorldAndProjXferBuf, SDL_GetError());
    }

    // Copy data to transfer buffer
    XFormBuffer* mappedData = reinterpret_cast<XFormBuffer*>(
        SDL_MapGPUTransferBuffer(m_GpuDevice->Device, m_WorldAndProjXferBuf, false));
    expect(mappedData, SDL_GetError());

    const MeshGroupCollection* meshGroups[] =
    {
        &m_CurrentState->m_OpaqueMeshGroups,
        &m_CurrentState->m_TranslucentMeshGroups
    };

    // Use inverse of camera transform as view matrix
    const Mat44f viewXform = camera.Inverse();

    // Projection transform
    const Mat44f viewProj = projection.Mul(viewXform);

    unsigned meshCount = 0;

    for(const auto meshGrpPtr : meshGroups)
    {
        for (auto& [mtlId, xmeshes] : *meshGrpPtr)
        {
            for (auto& xmesh : xmeshes)
            {
                mappedData[meshCount] =//
                {
                    .ModelXform = xmesh.WorldTransform,
                    .ModelViewProjXform = viewProj.Mul(xmesh.WorldTransform),
                };

                ++meshCount;
            }
        }
    }

    SDL_UnmapGPUTransferBuffer(m_GpuDevice->Device, m_WorldAndProjXferBuf);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(cmdBuf);
    expect(copyPass, SDL_GetError());

    SDL_GPUTransferBufferLocation src//
    {
        .transfer_buffer = m_WorldAndProjXferBuf,
        .offset = 0,
    };

    SDL_GPUBufferRegion dst//
    {
        .buffer = m_WorldAndProjBuf,
        .offset = 0,
        .size = static_cast<uint32_t>(sizeofTransformBuffer),
    };

    SDL_UploadToGPUBuffer(copyPass, &src, &dst, false);

    SDL_EndGPUCopyPass(copyPass);

    return Result<void>::Success;
}

Result<GpuTexture*>
SdlRenderer::GetDefaultBaseTexture()
{
    if(!m_DefaultBaseTexture)
    {
        static constexpr const char* MAGENTA_TEXTURE_KEY = "$magenta";

        auto result = m_GpuDevice->CreateTexture("#FF00FFFF"_rgba, imstring(MAGENTA_TEXTURE_KEY));
        expect(result, result.error());

        m_DefaultBaseTexture = result.value();
    }

    return m_DefaultBaseTexture;
}