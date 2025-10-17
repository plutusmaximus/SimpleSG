#include "SDLRenderGraph.h"

#include "Camera.h"

#include "Error.h"

#include "AutoDeleter.h"

#include "SDLGPUDevice.h"

#include <SDL3/SDL_gpu.h>

SdlRenderGraph::SdlRenderGraph(SDL_Window* window, SDL_GPUDevice* gpuDevice, RefPtr<MaterialDb> materialDb)
    : m_MaterialDb(materialDb)
    , m_Window(window)
    , m_GpuDevice(gpuDevice)
{
}

SdlRenderGraph::~SdlRenderGraph()
{
    SDL_ReleaseGPUTexture(m_GpuDevice, m_DepthBuffer);

    SDL_ReleaseGPUGraphicsPipeline(m_GpuDevice, m_Pipeline);
}

void
SdlRenderGraph::Add(const Mat44f& transform, RefPtr<Model> model)
{
    const int xfomIdx = static_cast<int>(m_Transforms.size());
    m_Transforms.push_back(transform);

    for (const auto& mesh : model->Meshes)
    {
        const MaterialId mtlId = mesh->MaterialId;

        if (!Verify(m_MaterialDb->Contains(mtlId)))
        {
            continue;
        }

        m_MeshGroups[mtlId].push_back({ xfomIdx, mesh });
    }
}

std::expected<void, Error>
SdlRenderGraph::Render(const Camera& camera)
{
    if (!m_Pipeline)
    {
        auto pipelineResult = CreatePipeline(m_Window);
        expect(pipelineResult, pipelineResult.error());

        m_Pipeline = pipelineResult.value();
    }

    SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(m_GpuDevice);

    expect(cmdBuf, SDL_GetError());

    SDL_GPUTexture* swapChainTexture;
    uint32_t windowW, windowH;
    expect(SDL_WaitAndAcquireGPUSwapchainTexture(cmdBuf, m_Window, &swapChainTexture, &windowW, &windowH), SDL_GetError());

    if (nullptr == swapChainTexture)
    {
        //Perhaps window minimized
        SDL_CancelGPUCommandBuffer(cmdBuf);
        std::this_thread::yield();
        return {};
    }

    auto adCmdBuf = AutoDeleter(SDL_SubmitGPUCommandBuffer, cmdBuf);

    if (!m_DepthBuffer || m_DepthCreateInfo.width != windowW || m_DepthCreateInfo.height != windowH)
    {
        SDL_ReleaseGPUTexture(m_GpuDevice, m_DepthBuffer);
        m_DepthBuffer = nullptr;

        m_DepthCreateInfo.width = windowW;
        m_DepthCreateInfo.height = windowH;

        m_DepthBuffer = SDL_CreateGPUTexture(m_GpuDevice, &m_DepthCreateInfo);
        expect(m_DepthBuffer, SDL_GetError());
    }

    SDL_GPUColorTargetInfo colorTargetInfo
    {
        .texture = swapChainTexture,
        .mip_level = 0,
        .layer_or_depth_plane = 0,
        .clear_color = {0, 0, 0, 0},
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE
    };

    SDL_GPUDepthStencilTargetInfo depthTargetInfo
    {
        .texture = m_DepthBuffer,
        .clear_depth = 1,
        .load_op = SDL_GPU_LOADOP_CLEAR,
        .store_op = SDL_GPU_STOREOP_STORE
    };

    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(
        cmdBuf,
        &colorTargetInfo,
        1,
        &depthTargetInfo);

    expect(renderPass, SDL_GetError());

    SDL_BindGPUGraphicsPipeline(renderPass, m_Pipeline);

    SDL_GPUViewport viewport
    {
        0, 0, (float)windowW, (float)windowH, 0, 1
    };
    SDL_SetGPUViewport(renderPass, &viewport);

    for (auto& [mtlId, xmeshes] : m_MeshGroups)
    {
        auto mtl = m_MaterialDb->GetMaterial(mtlId);

        SDL_PushGPUVertexUniformData(cmdBuf, 1, &mtl->Color, sizeof(mtl->Color));

        //const int idx = m_MaterialDb->GetIndex(mtlId);

        //SDL_PushGPUVertexUniformData(m_CmdBuf, 2, &idx, sizeof(idx));
        const int idx = 0;
        SDL_PushGPUVertexUniformData(cmdBuf, 2, &idx, sizeof(idx));

        // Bind texture and sampler
        SDL_GPUTextureSamplerBinding samplerBinding
        {
            .texture = (SDL_GPUTexture*)mtl->Albedo->GetTexture(),  //DO NOT SUBMIT
            .sampler = mtl->AlbedoSampler->Get()
        };
        SDL_BindGPUFragmentSamplers(renderPass, 0, &samplerBinding, 1);

        for (auto& xmesh : xmeshes)
        {
            const Mat44 xform = m_Transforms[xmesh.TransformIdx].Mul(camera.ViewProj());

            SDL_PushGPUVertexUniformData(cmdBuf, 0, &xform, sizeof(xform));

            SDL_GPUBufferBinding vertexBufferBinding
            {
                .buffer = (SDL_GPUBuffer*)xmesh.Mesh->VtxBuffer->GetBuffer(),
                .offset = 0
            };
            SDL_BindGPUVertexBuffers(renderPass, 0, &vertexBufferBinding, 1);

            SDL_GPUBufferBinding indexBufferBinding
            {
                .buffer = (SDL_GPUBuffer*)xmesh.Mesh->IdxBuffer->GetBuffer(),
                .offset = 0
            };
            SDL_BindGPUIndexBuffer(renderPass, &indexBufferBinding, SDL_GPU_INDEXELEMENTSIZE_16BIT);

            SDL_DrawGPUIndexedPrimitives(renderPass, xmesh.Mesh->IndexCount, 1, xmesh.Mesh->IndexOffset, 0, 0);
        }
    }

    SDL_EndGPURenderPass(renderPass);

    adCmdBuf.Cancel();

    auto fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdBuf);

    expect(fence, SDL_GetError());

    expect(SDL_WaitForGPUFences(m_GpuDevice, true, &fence, 1), SDL_GetError())

    SDL_ReleaseGPUFence(m_GpuDevice, fence);

    return {};
}

void
SdlRenderGraph::Reset()
{
    m_Transforms.clear();
    for (auto& [mtlId, meshes] : m_MeshGroups)
    {
        meshes.clear();
    }
}

//#define GPU_DRIVER_DIRECT3D
#define GPU_DRIVER_VULKAN

#if defined(GPU_DRIVER_DIRECT3D)

static const SDL_GPUShaderFormat SHADER_FORMAT = SDL_GPU_SHADERFORMAT_DXIL;
static const char* const DRIVER_NAME = "direct3d12";
static const char* const SHADER_EXTENSION = ".dxil";

#elif defined(GPU_DRIVER_VULKAN)

static const SDL_GPUShaderFormat SHADER_FORMAT = SDL_GPU_SHADERFORMAT_SPIRV;
static const char* const DRIVER_NAME = "vulkan";
static const char* const SHADER_EXTENSION = ".spv";

#else

#error Must define a GPU driver to use.

#endif

static std::expected<SDL_GPUShader*, Error> LoadVertexShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    const int numUniformBuffers);

static std::expected<SDL_GPUShader*, Error> LoadFragmentShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    const int numSamplers);

std::expected<SDL_GPUGraphicsPipeline*, Error>
SdlRenderGraph::CreatePipeline(SDL_Window* window)
{
    // Create shaders
    const std::string vshaderFileName = std::string("shaders/Debug/VertexShader") + SHADER_EXTENSION;
    auto vtxShaderResult = LoadVertexShader(m_GpuDevice, vshaderFileName, 3);
    expect(vtxShaderResult, vtxShaderResult.error());
    RefPtr<SdlResource<SDL_GPUShader>> vtxShader = new SdlResource<SDL_GPUShader>(m_GpuDevice, vtxShaderResult.value());

    const std::string fshaderFileName = std::string("shaders/Debug/FragmentShader") + SHADER_EXTENSION;
    auto fragShaderResult = LoadFragmentShader(m_GpuDevice, fshaderFileName, 1);
    expect(fragShaderResult, fragShaderResult.error());
    RefPtr<SdlResource<SDL_GPUShader>> fragShader = new SdlResource<SDL_GPUShader>(m_GpuDevice, fragShaderResult.value());

    SDL_GPUVertexBufferDescription vertexBufDescriptions[1] =
    {
        {
            .slot = 0,
            .pitch = sizeof(Vertex),
            .input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX
        }
    };
    SDL_GPUVertexAttribute vertexAttributes[] =
    {
        {.location = 0, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(Vertex, pos) },
        {.location = 1, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT3, .offset = offsetof(Vertex, normal) },
        {.location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(Vertex, uvs) }
    };

    SDL_GPUColorTargetDescription colorTargetDesc
    {
        .format = SDL_GetGPUSwapchainTextureFormat(m_GpuDevice, window),
        .blend_state = {}
    };

    SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo
    {
        .vertex_shader = vtxShaderResult.value(),
        .fragment_shader = fragShaderResult.value(),
        .vertex_input_state =
        {
            .vertex_buffer_descriptions = vertexBufDescriptions,
            .num_vertex_buffers = 1,
            .vertex_attributes = vertexAttributes,
            .num_vertex_attributes = std::size(vertexAttributes)
        },
        .primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST,
        .rasterizer_state =
        {
            .fill_mode = SDL_GPU_FILLMODE_FILL,
            .cull_mode = SDL_GPU_CULLMODE_BACK,
            .front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE,
            .enable_depth_clip = true
        },
        .depth_stencil_state =
        {
            .compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL,
            .enable_depth_test = true,
            .enable_depth_write = true
        },
        .target_info =
        {
            .color_target_descriptions = &colorTargetDesc,
            .num_color_targets = 1,
            .depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            .has_depth_stencil_target = true
        }
    };

    SDL_GPUGraphicsPipeline* pipeline = SDL_CreateGPUGraphicsPipeline(m_GpuDevice, &pipelineCreateInfo);
    expect(pipeline, SDL_GetError());

    return pipeline;
}

static std::expected<SDL_GPUShader*, Error> LoadShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    SDL_GPUShaderStage shaderStage,
    const unsigned numUniformBuffers,
    const unsigned numSamplers)
{
    size_t fileSize;
    void* shaderSrc = SDL_LoadFile(fileName.data(), &fileSize);
    expect(shaderSrc, fileName, SDL_GetError());

    auto ad = AutoDeleter(SDL_free, shaderSrc);

    SDL_GPUShaderCreateInfo shaderCreateInfo
    {
        .code_size = fileSize,
        .code = (uint8_t*)shaderSrc,
        .entrypoint = "main",
        .format = SHADER_FORMAT,
        .stage = shaderStage,
        .num_samplers = numSamplers,
        .num_uniform_buffers = numUniformBuffers
    };

    SDL_GPUShader* shader = SDL_CreateGPUShader(gpuDevice, &shaderCreateInfo);
    expect(shader, SDL_GetError());

    return shader;
}

static std::expected<SDL_GPUShader*, Error> LoadVertexShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    const int numUniformBuffers)
{
    return LoadShader(gpuDevice, fileName, SDL_GPU_SHADERSTAGE_VERTEX, numUniformBuffers, 0);
}

static std::expected<SDL_GPUShader*, Error> LoadFragmentShader(
    SDL_GPUDevice* gpuDevice,
    const std::string_view fileName,
    const int numSamplers)
{
    return LoadShader(gpuDevice, fileName, SDL_GPU_SHADERSTAGE_FRAGMENT, 0, numSamplers);
}