#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <thread>
#include <filesystem>

#include "Error.h"
#include "ModelVisitor.h"
#include "SdlHelpers.h"
#include "ModelNode.h"
#include "TransformNode.h"
#include "MaterialDb.h"
#include "SdlRenderGraph.h"
#include "VecMath.h"

#include "SDLGPUDevice.h"

// Cube vertices (8 corners with positions, normals, and colors)
Vertex cubeVertices[] =
{
    // Front face
    {{-0.5f, -0.5f,  0.5f}, {0.0f,  0.0f,  1.0f},  {1, 1}}, // 0
    {{ 0.5f, -0.5f,  0.5f}, {0.0f,  0.0f,  1.0f},  {0, 1}}, // 1
    {{ 0.5f,  0.5f,  0.5f}, {0.0f,  0.0f,  1.0f},  {0, 0}}, // 2
    {{-0.5f,  0.5f,  0.5f}, {0.0f,  0.0f,  1.0f},  {1, 0}}, // 3
    // Back face
    {{-0.5f, -0.5f, -0.5f}, {0.0f,  0.0f, -1.0f},  {0, 1}}, // 4
    {{ 0.5f, -0.5f, -0.5f}, {0.0f,  0.0f, -1.0f},  {1, 1}}, // 5
    {{ 0.5f,  0.5f, -0.5f}, {0.0f,  0.0f, -1.0f},  {1, 0}}, // 6
    {{-0.5f,  0.5f, -0.5f}, {0.0f,  0.0f, -1.0f},  {0, 0}}, // 7
    // Left face
    {{-0.5f, -0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f},  {1, 1}}, // 8
    {{-0.5f, -0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f},  {0, 1}}, // 9
    {{-0.5f,  0.5f,  0.5f}, {-1.0f,  0.0f,  0.0f},  {0, 0}}, // 10
    {{-0.5f,  0.5f, -0.5f}, {-1.0f,  0.0f,  0.0f},  {1, 0}}, // 11
    // Right face
    {{0.5f, -0.5f,  0.5f}, {1.0f,  0.0f,  0.0f},  {1, 1}}, // 12
    {{0.5f, -0.5f, -0.5f}, {1.0f,  0.0f,  0.0f},  {0, 1}}, // 13
    {{0.5f,  0.5f, -0.5f}, {1.0f,  0.0f,  0.0f},  {0, 0}}, // 14
    {{0.5f,  0.5f,  0.5f}, {1.0f,  0.0f,  0.0f},  {1, 0}}, // 15
    // Top face
    {{-0.5f,  0.5f,  0.5f}, {0.0f,  1.0f,  0.0f},  {0, 0}}, // 16
    {{ 0.5f,  0.5f,  0.5f}, {0.0f,  1.0f,  0.0f},  {1, 0}}, // 17
    {{ 0.5f,  0.5f, -0.5f}, {0.0f,  1.0f,  0.0f},  {1, 1}}, // 18
    {{-0.5f,  0.5f, -0.5f}, {0.0f,  1.0f,  0.0f},  {0, 1}}, // 19
    // Bottom face
    {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f,  0.0f},  {0, 1}}, // 20
    {{ 0.5f, -0.5f, -0.5f}, {0.0f, -1.0f,  0.0f},  {1, 1}}, // 21
    {{ 0.5f, -0.5f,  0.5f}, {0.0f, -1.0f,  0.0f},  {1, 0}}, // 22
    {{-0.5f, -0.5f,  0.5f}, {0.0f, -1.0f,  0.0f},  {0, 0}}, // 23
};

// Cube indices (36 indices for 12 triangles, 2 per face, all in CCW order)
Uint16 cubeIndices[] =
{
    // Front (z = 0.5, normal +z, view from front)
    0, 3, 2,  0, 2, 1,    // CCW: bottom-left -> top-left -> top-right, bottom-left -> top-right -> bottom-right
    // Back (z = -0.5, normal -z, view from back)
    5, 6, 7,  5, 7, 4,    // CCW: bottom-right -> top-right -> top-left, bottom-right -> top-left -> bottom-left
    // Left (x = -0.5, normal -x, view from left)
    11, 10, 9,  8, 11, 9, // CCW: front-bottom -> front-top -> back-top, front-bottom -> back-top -> back-bottom
    // Right (x = 0.5, normal +x, view from right)
    15, 14, 13,  12, 15, 13, // CCW: back-bottom -> back-top -> front-top, back-bottom -> front-top -> front-bottom
    // Top (y = 0.5, normal +y, view from top)
    18, 17, 16,  19, 18, 16, // CCW: front-left -> front-right -> back-right, front-left -> back-right -> back-left
    // Bottom (y = -0.5, normal -y, view from bottom)
    20, 23, 22,  20, 22, 21  // CCW: back-left -> front-left -> front-right, back-left -> front-right -> back-right
};

RgbaColorf Colors[] =
{
    {1, 0, 0},
    {0, 1, 0},
    {0, 0, 1},
    {1, 1, 1},
    {0, 1, 1},
    {1, 0, 1}
};

// Set up vertex data for quad
Vertex quadVertices[] =
{
    {{-0.5f, -0.5f,  0.5f}, {0.0f,  0.0f,  1.0f},  {0, 1}}, // 0
    {{ 0.5f, -0.5f,  0.5f}, {0.0f,  0.0f,  1.0f},  {1, 1}}, // 1
    {{ 0.5f,  0.5f,  0.5f}, {0.0f,  0.0f,  1.0f},  {1, 0}}, // 2
    {{-0.5f,  0.5f,  0.5f}, {0.0f,  0.0f,  1.0f},  {0, 0}}, // 3
};
uint16_t quadIndices[] = { 0, 3, 2,  0, 2, 1 };

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
    spdlog::set_level(spdlog::level::debug);

    etry
    {
        //DO NOT SUBMIT - path
        const std::string texPaths[] =
        {
            "Images\\Ant.png",
            "Images\\Bee.png",
            "Images\\Butterfly.png",
            "Images\\Frog.png",
            "Images\\Lizard.png",
            "Images\\Turtle.png",
        };

        auto gdResult = SDLGPUDevice::Create();
        pcheck(gdResult, gdResult.error());
        auto gd = *gdResult;
        SDL_GPUDevice* gpuDevice = (SDL_GPUDevice*)gd->GetDevice();//DO NOT SUBMIT
        SDL_Window* window = (SDL_Window*)gd->GetWindow();//DO NOT SUBMIT

        RefPtr<MaterialDb> materialDb = MaterialDb::Create();
        std::vector<MaterialId> materialIds;

        constexpr int NUM_MATERIALS = 6;

        for (int i = 0; i < NUM_MATERIALS; ++i)
        {
            const auto& path = texPaths[i % std::size(texPaths)];
            const auto& color = Colors[i % std::size(Colors)];

            Material::Spec mtlSpec
            {
                .Color = color,
                .Albedo = path
            };
            auto materialResult = Material::Create(gd, mtlSpec);

            materialDb->Add(materialResult.value());

            materialIds.push_back(materialResult.value()->Id);
        }

        // Create meshes
        auto cubeVb = gd->CreateVertexBuffer(cubeVertices, std::size(cubeVertices));
        pcheck(cubeVb, cubeVb.error());
        auto cubeIb = gd->CreateIndexBuffer(cubeIndices, std::size(cubeIndices));
        pcheck(cubeIb, cubeVb.error());

        RefPtr<Mesh> cubeMeshes[]
        {
            Mesh::Create(cubeVb.value(), cubeIb.value(), 0, 6, materialIds[0]),
            Mesh::Create(cubeVb.value(), cubeIb.value(), 6, 6, materialIds[1]),
            Mesh::Create(cubeVb.value(), cubeIb.value(), 12, 6, materialIds[2]),
            Mesh::Create(cubeVb.value(), cubeIb.value(), 18, 6, materialIds[3]),
            Mesh::Create(cubeVb.value(), cubeIb.value(), 24, 6, materialIds[4]),
            Mesh::Create(cubeVb.value(), cubeIb.value(), 30, 6, materialIds[5])
        };

        RefPtr<Model> cubeModel = Model::Create(cubeMeshes);
        pcheck(cubeModel, "Model::Create failed");

        RefPtr<GroupNode> scene = new GroupNode();

        RefPtr<ModelNode> planetModelNode = new ModelNode(cubeModel);
        RefPtr<TransformNode> planetXFormNode = new TransformNode();
        RefPtr<ModelNode> moonModelNode = new ModelNode(cubeModel);
        RefPtr<TransformNode> moonXFormNode = new TransformNode();
        planetXFormNode->AddChild(planetModelNode);
        moonXFormNode->AddChild(moonModelNode);
        planetXFormNode->AddChild(moonXFormNode);
        scene->AddChild(planetXFormNode);


        auto quadVb = gd->CreateVertexBuffer(quadVertices);
        pcheck(quadVb, quadVb.error());
        auto quadIb = gd->CreateIndexBuffer(quadIndices);
        pcheck(quadIb, quadIb.error());

        RefPtr<Mesh> quadMeshes[]
        {
            Mesh::Create(quadVb.value(), quadIb.value(), 0, std::size(quadIndices), materialIds[0])
        };

        RefPtr<Model> quadModel = Model::Create(quadMeshes);
        pcheck(quadModel, "Model::Create failed");

        RefPtr<ModelNode> quadModelNode = new ModelNode(quadModel);
        RefPtr<TransformNode> quadXFormNode = new TransformNode();
        quadXFormNode->AddChild(quadModelNode);

        // Create shaders
        const std::string vshaderFileName = std::string("shaders/Debug/VertexShader") + SHADER_EXTENSION;
        SdlResource<SDL_GPUShader> vtxShader(gpuDevice, LoadVertexShader(gpuDevice, vshaderFileName, 3));
        pcheck(vtxShader, "LoadVertexShader({}) failed", vshaderFileName);

        const std::string fshaderFileName = std::string("shaders/Debug/FragmentShader") + SHADER_EXTENSION;
        SdlResource<SDL_GPUShader> fragShader(gpuDevice, LoadFragmentShader(gpuDevice, fshaderFileName, 1));
        pcheck(vtxShader, "LoadFragmentShader({}) failed", fshaderFileName);

        SDL_GPUTextureCreateInfo depthCreateInfo
        {
            .type = SDL_GPU_TEXTURETYPE_2D,
            .format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
            .usage = SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET,
            .width = 0,
            .height = 0,
            .layer_count_or_depth = 1,
            .num_levels = 1
        };

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
            {.location = 2, .buffer_slot = 0, .format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2, .offset = offsetof(Vertex, uv) }
        };

        SDL_GPUColorTargetDescription colorTargetDesc
        {
            .format = SDL_GetGPUSwapchainTextureFormat(gpuDevice, window),
            .blend_state = {}
        };

        SDL_GPUGraphicsPipelineCreateInfo pipelineCreateInfo
        {
            .vertex_shader = vtxShader.Get(),
            .fragment_shader = fragShader.Get(),
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
                .depth_stencil_format = depthCreateInfo.format,
                .has_depth_stencil_target = true
            }
        };

        // Create pipeline
        SdlResource<SDL_GPUGraphicsPipeline> pipeline(gpuDevice, SDL_CreateGPUGraphicsPipeline(gpuDevice, &pipelineCreateInfo));
        pcheck(pipeline, "SDL_CreateGPUGraphicsPipeline: {}", SDL_GetError());

        RefPtr<SdlResource<SDL_GPUTexture>> depthBuffer;

        const Degreesf fov(45);
        Camera camera(fov, 1, 0.1f, 100);

        // Main loop
        bool running = true;
        Radiansf planetSpinAngle(0), moonSpinAngle(0), moonOrbitAngle(0);
        while (running)
        {
            SDL_Event event;
            bool minimized = false;
            while (running && (minimized ? SDL_WaitEvent(&event) : SDL_PollEvent(&event)))
            {
                if (event.type == SDL_EVENT_QUIT)
                {
                    running = false;
                }
                else if (event.type == SDL_EVENT_WINDOW_MINIMIZED)
                {
                    minimized = true;
                }
                else if (event.type == SDL_EVENT_WINDOW_RESTORED)
                {
                    minimized = false;
                }
                else if (event.type == SDL_EVENT_WINDOW_MAXIMIZED)
                {
                    minimized = false;
                }
            }

            if (!running)
            {
                continue;
            }

            // Update model matrix
            planetSpinAngle += 0.001f;
            moonSpinAngle += 0.005f;
            moonOrbitAngle += 0.005f;
            
            planetXFormNode->Transform =
                Mat44f::Identity()
                .Rotate(Quaternionf(planetSpinAngle, Vec3f::YAXIS))
                .Rotate(Degreesf(15), Vec3f::ZAXIS)
                .Translate(0, 0, 4);
            moonXFormNode->Transform =
                Mat44f::Identity()
                .Scale(0.25)
                .Rotate(Quaternionf(-moonSpinAngle, Vec3f::YAXIS))
                .Translate(0, 0, -2)
                .Rotate(Quaternionf(-moonOrbitAngle, Vec3f::YAXIS));

            SDL_GPUCommandBuffer* cmdBuf = SDL_AcquireGPUCommandBuffer(gpuDevice);

            SDL_GPUTexture* swapChainTexture;
            uint32_t windowW, windowH;
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmdBuf, window, &swapChainTexture, &windowW, &windowH))
            {
                LOG_EXPR_ERROR("SDL_WaitAndAcquireGPUSwapchainTexture: {}", SDL_GetError());
            }

            if (nullptr == swapChainTexture)
            {
                //Perhaps window minimized
                SDL_CancelGPUCommandBuffer(cmdBuf);
                std::this_thread::yield();
                continue;
            }

            if (depthCreateInfo.width != windowW || depthCreateInfo.height != windowH)
            {
                depthCreateInfo.width = windowW;
                depthCreateInfo.height = windowH;

                depthBuffer = new SdlResource<SDL_GPUTexture>(gpuDevice, SDL_CreateGPUTexture(gpuDevice, &depthCreateInfo));
                pcheck(*depthBuffer, "SDL_CreateGPUTexture failed");

                camera.SetAspect(float(windowW) / windowH);
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
                .texture = (*depthBuffer).Get(),
                .clear_depth = 1,
                .load_op = SDL_GPU_LOADOP_CLEAR,
                .store_op = SDL_GPU_STOREOP_STORE
            };

            SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(
                cmdBuf,
                &colorTargetInfo,
                1,
                &depthTargetInfo);

            if (!renderPass)
            {
                LOG_EXPR_ERROR("SDL_BeginGPURenderPass: {}", SDL_GetError());
                SDL_CancelGPUCommandBuffer(cmdBuf);
                std::this_thread::yield();
                continue;
            }

            SdlRenderGraph renderGraph(materialDb, cmdBuf, renderPass);

            SDL_BindGPUGraphicsPipeline(renderPass, pipeline.Get());

            SDL_GPUViewport viewport
            {
                0, 0, (float)windowW, (float)windowH, 0, 1
            };
            SDL_SetGPUViewport(renderPass, &viewport);

            ModelVisitor visitor(&renderGraph, camera);

            scene->Accept(&visitor);

            renderGraph.Render(camera);

            SDL_EndGPURenderPass(renderPass);

            auto fence = SDL_SubmitGPUCommandBufferAndAcquireFence(cmdBuf);

            if (!fence)
            {
                LOG_EXPR_ERROR("SDL_SubmitGPUCommandBufferAndAcquireFence: {}", SDL_GetError());
                continue;
            }

            if (!SDL_WaitForGPUFences(gpuDevice, true, &fence, 1))
            {
                LOG_EXPR_ERROR("SDL_WaitForGPUFences: {}", SDL_GetError());
            }

            SDL_ReleaseGPUFence(gpuDevice, fence);
        }
    }
    ecatchall;

    SDL_Quit();

    return 0;
}