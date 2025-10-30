#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <filesystem>

#include "Error.h"
#include "ModelVisitor.h"
#include "TransformNode.h"
#include "SDLRenderGraph.h"
#include "VecMath.h"
#include "Camera.h"

#include "SDLGPUDevice.h"

#include "STLLoader.h"

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

VertexIndex cubeIndices[] =
{
    // Front
    0, 2, 3,  0, 1, 2,
    // Back
    5, 7, 6,  5, 4, 7,
    // Left
    11, 9, 10,  8, 9, 11,
    // Right
    15, 13, 14,  12, 13, 15,
    // Top
    18, 16, 17,  19, 16, 18,
    // Bottom
    20, 22, 23,  20, 21, 22
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
            TVertex vertices[] =
            {
                //Change winding from CCW to CW
                tri.v[0], tri.v[2], tri.v[1]
            };

            for (auto& v : vertices)
            {
                //STL uses a right handed coordinate system with
                //Z up, Y into the screen, triangles winding counter clockwise.
                //This swap changes to a left handed coordinate system
                //with Y up, Z into the screen, and triangles winding clockwise.
                std::swap(v.pos.y, v.pos.z);
                std::swap(v.normal.y, v.normal.z);
            }

            for(int i = 0; i < 3; ++i)
            {
                const TVertex& tv = vertices[i];
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
                const Vec3f& v1 = vertices[(i + 1) % 3].pos;
                const Vec3f& v2 = vertices[(i + 2) % 3].pos;

                Vec3 normal = (v1 - v0).Cross(v2 - v0).Normalize();
                pumpkinVertices[index].normal = pumpkinVertices[index].normal + normal;

                pumpkinIndices.push_back(index);
            }
        }

        Assert(false);

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
        Camera camera(fov, 100, 100, 0.1f, 1000);

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

            constexpr Radiansf planetTiltAngle = Radiansf::FromDegrees(15);

            planetXFormNode->Transform =
                Mat44f::Identity()
                .Translate(0, 0, 4)
                //.Translate(0, 0, 150)
                .Rotate(planetTiltAngle, Vec3f::ZAXIS()) //tilt
                .Rotate(planetSpinAngle, Vec3f::YAXIS());  //spin

            moonXFormNode->Transform =
                Mat44f::Identity()
                .Rotate(moonOrbitAngle, Vec3f::YAXIS())    //orbit
                .Translate(0, 0, -2)
                //.Translate(0, 0, -100)
                .Rotate(moonSpinAngle, Vec3f::YAXIS())  //spin
                .Scale(0.25f);

            int windowW, windowH;
            if (!SDL_GetWindowSizeInPixels(window, &windowW, &windowH))
            {
                logError(SDL_GetError());
                continue;
            }

            camera.SetBounds(windowW, windowH);

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