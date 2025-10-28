#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <thread>
#include <filesystem>

#include "Error.h"
#include "ModelVisitor.h"
#include "TransformNode.h"
#include "SDLRenderGraph.h"
#include "VecMath.h"

#include "SDLGPUDevice.h"

#include "STLLoader.h"

#include "glm/glm.hpp"
#include "glm/ext/matrix_transform.hpp"

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
VertexIndex cubeIndices[] =
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

int main([[maybe_unused]] int argc, [[maybe_unused]] char* argv[])
{
    Logging::SetLogLevel(spdlog::level::trace);

    SDL_Window* window = nullptr;

    auto cwd = std::filesystem::current_path();

    etry
    {
        pcheck(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

        SDL_Rect displayRect;
        auto dm = SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &displayRect);
        const int winW = displayRect.w * 0.75;
        const int winH = displayRect.h * 0.75;

        // Create window
        window = SDL_CreateWindow("SDL3 GPU Cube", winW, winH, SDL_WINDOW_RESIZABLE);
        pcheck(window, SDL_GetError());

        auto gdResult = SDLGPUDevice::Create(window);
        pcheck(gdResult, gdResult.error());
        auto gd = *gdResult;

        std::vector<Triangle> triangles;
        auto stlResult = loadAsciiSTL(cwd.string() + "/Models/Pumpkin-DD.stl", triangles);
        pcheck(stlResult, stlResult.error());
        pcheck(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

        std::map<TVertex, unsigned> vmap;
        std::vector<Vertex> pumpkinVertices;
        std::vector<unsigned> pumpkinIndices;
        for (const auto& tri : triangles)
        {
            for(int i = 0; i < 3; ++i)
            {
                const TVertex& tv = tri.v[i];
                unsigned index;

                auto it = vmap.find(tv);
                if (vmap.end() == it)
                {
                    index = vmap.size();
                    vmap.emplace(tv, index);
                    pumpkinVertices.push_back(tv);
                    pumpkinVertices[index].normal = Vec3f(0, 0, 0);
                }
                else
                {
                    index = it->second;
                }

                const Vec3f& v0 = tv.pos;
                const Vec3f& v1 = tri.v[(i + 1) % 3].pos;
                const Vec3f& v2 = tri.v[(i + 2) % 3].pos;

                Vec3 normal = (v2 - v0).Cross(v1 - v0).Normalize();
                pumpkinVertices[index].normal = pumpkinVertices[index].normal + normal;

                pumpkinIndices.push_back(index);
            }
        }

        for (auto& v : pumpkinVertices)
        {
            v.normal = v.normal.Normalize();
        }

        MeshSpec pumpkinMeshSpecs[] =
        {
            {
                .IndexOffset = 0,
                .IndexCount = (unsigned)pumpkinIndices.size(),
                .MtlSpec =
                {
                    .Color = {1, 0, 0},
                    .VertexShader = "shaders/Debug/VertexShader",
                    .FragmentShader = "shaders/Debug/ColorShader",
                    .Albedo = "Images\\Ant.png"
                }
            },
        };

        ModelSpec pumpkinModelSpec
        {
            .Vertices = pumpkinVertices,
            .Indices = pumpkinIndices,
            .MeshSpecs = pumpkinMeshSpecs
        };

        MeshSpec cubeMeshSpecs[] =
        {
            {
                .IndexOffset = 0,
                .IndexCount = 6,
                .MtlSpec =
                {
                    .Color = {1, 0, 0},
                    .VertexShader = "shaders/Debug/VertexShader",
                    .FragmentShader = "shaders/Debug/FragmentShader",
                    .Albedo = "Images\\Ant.png"
                }
            },
            {
                .IndexOffset = 6,
                .IndexCount = 6,
                .MtlSpec =
                {
                    .Color = {0, 1, 0},
                    .VertexShader = "shaders/Debug/VertexShader",
                    .FragmentShader = "shaders/Debug/FragmentShader",
                    .Albedo = "Images\\Bee.png"
                }
            },
            {
                .IndexOffset = 12,
                .IndexCount = 6,
                .MtlSpec =
                {
                    .Color = {0, 0, 1},
                    .VertexShader = "shaders/Debug/VertexShader",
                    .FragmentShader = "shaders/Debug/FragmentShader",
                    .Albedo = "Images\\Butterfly.png"
                }
            },
            {
                .IndexOffset = 18,
                .IndexCount = 6,
                .MtlSpec =
                {
                    .Color = {1, 1, 1},
                    .VertexShader = "shaders/Debug/VertexShader",
                    .FragmentShader = "shaders/Debug/FragmentShader",
                    .Albedo = "Images\\Frog.png"
                }
            },
            {
                .IndexOffset = 24,
                .IndexCount = 6,
                .MtlSpec =
                {
                    .Color = {0, 1, 1},
                    .VertexShader = "shaders/Debug/VertexShader",
                    .FragmentShader = "shaders/Debug/FragmentShader",
                    .Albedo = "Images\\Lizard.png"
                }
            },
            {
                .IndexOffset = 30,
                .IndexCount = 6,
                .MtlSpec =
                {
                    .Color = {1, 0, 1},
                    .VertexShader = "shaders/Debug/VertexShader",
                    .FragmentShader = "shaders/Debug/FragmentShader",
                    .Albedo = "Images\\Turtle.png"
                }
            },
        };

        ModelSpec cubeModelSpec
        {
            .Vertices = cubeVertices,
            .Indices = cubeIndices,
            .MeshSpecs = cubeMeshSpecs
        };
        auto cubeModelResult = gd->CreateModel(cubeModelSpec);
        pcheck(cubeModelResult, cubeModelResult.error());
        auto cubeModel = cubeModelResult.value();

        auto pumpkinModelResult = gd->CreateModel(pumpkinModelSpec);
        pcheck(pumpkinModelResult, pumpkinModelResult.error());
        auto pumpkinModel = pumpkinModelResult.value();

        RefPtr<GroupNode> scene = new GroupNode();

        RefPtr<TransformNode> planetXFormNode = new TransformNode();
        RefPtr<TransformNode> moonXFormNode = new TransformNode();
        planetXFormNode->AddChild(cubeModel);
        moonXFormNode->AddChild(cubeModel);
        //planetXFormNode->AddChild(pumpkinModel);
        //moonXFormNode->AddChild(pumpkinModel);
        planetXFormNode->AddChild(moonXFormNode);
        scene->AddChild(planetXFormNode);

        const Degreesf fov(45);
        Camera camera(fov, 1, 0.1f, 1000);

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
            planetSpinAngle = (planetSpinAngle + 0.001f).Wrap();
            moonSpinAngle = (moonSpinAngle - 0.005f).Wrap();
            moonOrbitAngle = (moonOrbitAngle - 0.005f).Wrap();
            
            planetXFormNode->Transform =
                Mat44f::Identity()
                .Rotate(planetSpinAngle, Vec3f::YAXIS)  //spin
                .Rotate(Degreesf(15), Vec3f::ZAXIS) //tilt
                .Translate(0, 0, 4);
                //.Translate(0, 0, 150);

            moonXFormNode->Transform =
                Mat44f::Identity()
                .Scale(0.25f)
                .Rotate(moonSpinAngle, Vec3f::YAXIS)  //spin
                .Translate(0, 0, -2)
                //.Translate(0, 0, -100)
                .Rotate(moonOrbitAngle, Vec3f::YAXIS);    //orbit

            int windowW, windowH;
            if (!SDL_GetWindowSizeInPixels(window, &windowW, &windowH))
            {
                logError(SDL_GetError());
                continue;
            }

            camera.SetAspect(float(windowW) / windowH);

            auto renderGraphResult = gd->CreateRenderGraph();
            pcheck(renderGraphResult, renderGraphResult.error());

            auto renderGraph = renderGraphResult.value();

            ModelVisitor visitor(renderGraph);
            scene->Accept(&visitor);
            auto renderResult = renderGraph->Render(camera);
            if (!renderResult)
            {
                logError(renderResult.error().Message);
                continue;
            }
        }
    }
    ecatchall;

    if (window)
    {
        SDL_DestroyWindow(window);
    }

    SDL_Quit();

    return 0;
}