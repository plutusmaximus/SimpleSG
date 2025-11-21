#include <SDL3/SDL.h>
#include <SDL3/SDL_gpu.h>
#include <filesystem>

#include "ECS.h"

#include "Error.h"
#include "SceneVisitors.h"
#include "SceneNodes.h"
#include "SDLRenderGraph.h"
#include "VecMath.h"
#include "AutoDeleter.h"

#include "SDLGPUDevice.h"

#include "MouseNav.h"

#include "STLLoader.h"

#include "Shapes.h"

#include "LuaRepl.h"

#include "EcsTransformNodePool.h"

static Result<RefPtr<Model>> CreateCubeModel(RefPtr<GPUDevice> gpu);
static Result<RefPtr<Model>> CreatePumpkinModel(RefPtr<GPUDevice> gpu);
static Result<RefPtr<Model>> CreateShapeModel(RefPtr<GPUDevice> gpu);

static int Add(lua_State* L)
{
    int arg1 = luaL_checkinteger(L, 1);
    int arg2 = luaL_checkinteger(L, 2);
    lua_pushinteger(L, arg1 + arg2);
    return 1;
}

int main(int, [[maybe_unused]] char* argv[])
{
    Logging::SetLogLevel(spdlog::level::trace);

    LuaRepl repl;

    repl.ExportFunction(Add, "Add");

    auto cwd = std::filesystem::current_path();
    logInfo("Current working directory: {}", cwd.string());

    etry
    {
        pcheck(SDL_Init(SDL_INIT_VIDEO), SDL_GetError());

        SDL_Rect displayRect;
        auto dm = SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &displayRect);
        const int winW = displayRect.w * 0.75;
        const int winH = displayRect.h * 0.75;

        // Create window
        SDL_Window* window = SDL_CreateWindow("SDL3 GPU Cube", winW, winH, SDL_WINDOW_RESIZABLE);
        pcheck(window, SDL_GetError());

        auto windowDeleter = AutoDeleter(SDL_DestroyWindow, window);

        auto gdResult = SDLGPUDevice::Create(window);
        pcheck(gdResult, gdResult.error());
        auto gd = *gdResult;

        EcsRegistry reg;

        auto eplanet = reg.Create();
        auto eorbit = reg.Create();
        auto emoon = reg.Create();

        reg.Add<TransformNode2>(eplanet, TransformNode2{ .Id = eplanet });
        reg.Add<TransformNode2>(eorbit, TransformNode2{ .Id = eorbit, .ParentId = eplanet });
        reg.Add<TransformNode2>(emoon, TransformNode2{ .Id = emoon, .ParentId = eorbit });

        //auto modelResult = CreateCube(gd);
        //auto modelResult = CreatePumpkin(gd);
        auto modelResult = CreateShapeModel(gd);
        pcheck(modelResult, modelResult.error());
        auto model = modelResult.value();

        auto sceneResult = GroupNode::Create();
        pcheck(sceneResult, sceneResult.error());
        auto scene = sceneResult.value();

        auto planetResult = PropNode::Create(model);
        pcheck(planetResult, planetResult.error());
        auto planet = planetResult.value();

        auto moonOrbitResult = TransformNode::Create();
        pcheck(moonOrbitResult, moonOrbitResult.error());
        auto moonOrbit = moonOrbitResult.value();

        auto moonResult = PropNode::Create(model);
        pcheck(moonResult, moonResult.error());
        auto moon = moonResult.value();

        moonOrbit->AddChild(moon);
        planet->AddChild(moonOrbit);
        scene->AddChild(planet);

        auto camXformNodeResult = TransformNode::Create();
        pcheck(camXformNodeResult, camXformNodeResult.error());
        auto camXformNode = camXformNodeResult.value();

        auto cameraResult = CameraNode::Create();
        pcheck(cameraResult, cameraResult.error());
        RefPtr<CameraNode> camera = cameraResult.value();
        const Degreesf fov(45);
        camera->SetPerspective(fov, 100, 100, 0.1f, 1000);
        camXformNode->AddChild(camera);
        scene->AddChild(camXformNode);

        camXformNode->Transform.T =Vec3f{0,0,-4};

        // Main loop
        bool running = true;
        Radiansf planetSpinAngle(0), moonSpinAngle(0), moonOrbitAngle(0);

        GimbleMouseNav gimbleMouseNav(camXformNode);
        MouseNav* mouseNav = &gimbleMouseNav;

        while (running)
        {
            int windowW, windowH;
            if (!SDL_GetWindowSizeInPixels(window, &windowW, &windowH))
            {
                logError(SDL_GetError());
                continue;
            }

            SDL_Event event;
            bool minimized = false;
            while (running && (minimized ? SDL_WaitEvent(&event) : SDL_PollEvent(&event)))
            {
                switch (event.type)
                {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;

                case SDL_EVENT_WINDOW_MINIMIZED:
                    minimized = true;
                    break;

                case SDL_EVENT_WINDOW_RESTORED:
                case SDL_EVENT_WINDOW_MAXIMIZED:
                    minimized = false;
                    break;

                case SDL_EVENT_WINDOW_MOUSE_LEAVE:
                case SDL_EVENT_WINDOW_FOCUS_GAINED:
                case SDL_EVENT_WINDOW_FOCUS_LOST:
                    mouseNav->ClearButtons();
                    break;

                case SDL_EVENT_MOUSE_MOTION:
                    mouseNav->OnMouseMove(Vec2f(event.motion.xrel, event.motion.yrel));
                    break;

                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    mouseNav->OnMouseDown(Vec2f(event.button.x, event.button.y), Vec2f(windowW, windowH), event.button.button - 1);
                    break;
                case SDL_EVENT_MOUSE_BUTTON_UP:
                    mouseNav->OnMouseUp(event.button.button - 1);
                    break;

                case SDL_EVENT_MOUSE_WHEEL:
                    mouseNav->OnScroll(Vec2f(event.wheel.x, event.wheel.y));
                    break;

                case SDL_EVENT_KEY_DOWN:
                    mouseNav->OnKeyDown(event.key.scancode);
                    break;

                case SDL_EVENT_KEY_UP:
                    mouseNav->OnKeyUp(event.key.scancode);
                    break;
                }
            }

            if (!running)
            {
                continue;
            }

            repl.Update();

            // Update model matrix
            planetSpinAngle = (planetSpinAngle + 0.001f).Wrap();
            moonSpinAngle = (moonSpinAngle - 0.005f).Wrap();
            moonOrbitAngle = (moonOrbitAngle - 0.005f).Wrap();

            const Quatf planetTilt{ Radiansf::FromDegrees(15), Vec3f::ZAXIS() };

            planet->Transform.R = planetTilt * Quatf{ planetSpinAngle, Vec3f::YAXIS() };

            moonOrbit->Transform.R = Quatf{ moonOrbitAngle, Vec3f::YAXIS() };

            moon->Transform.T = Vec3f{ 0,0,-2 };
            moon->Transform.R = Quatf{ moonSpinAngle, Vec3f::YAXIS() };
            moon->Transform.S = Vec3f{ 0.25f };

            camera->SetBounds(windowW, windowH);

            SDLRenderGraph renderGraph(gd.Get());

            CameraVisitor cameraVisitor;
            ModelVisitor modelVisitor(&renderGraph);
            scene->Accept(&cameraVisitor);
            scene->Accept(&modelVisitor);

            auto vsCamera = cameraVisitor.GetCameras().front();
            auto renderResult = renderGraph.Render(vsCamera.Transform, vsCamera.Projection);
            if (!renderResult)
            {
                logError(renderResult.error().Message);
                continue;
            }
        }
    }
    ecatchall;

    SDL_Quit();

    return 0;
}

// Cube vertices (8 corners with positions, normals, and colors)
static Vertex cubeVertices[] =
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

static VertexIndex cubeIndices[] =
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

static Result<RefPtr<Model>> CreateCubeModel(RefPtr<GPUDevice> gpu)
{
    MeshSpec meshSpecs[] =
    {
        {
            .IndexOffset = 0,
            .IndexCount = 6,
            .MtlSpec =
            {
                .Color = {1, 0, 0},
                .VertexShader = "shaders/Debug/VertexShader",
                .FragmentShader = "shaders/Debug/FragmentShader",
                .Albedo = "images/Ant.png"
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
                .Albedo = "images/Bee.png"
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
                .Albedo = "images/Butterfly.png"
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
                .Albedo = "images/Frog.png"
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
                .Albedo = "images/Lizard.png"
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
                .Albedo = "images/Turtle.png"
            }
        },
    };

    ModelSpec modelSpec
    {
        .Vertices = cubeVertices,
        .Indices = cubeIndices,
        .MeshSpecs = meshSpecs
    };

    return gpu->CreateModel(modelSpec);
}

static Result<RefPtr<Model>> CreateShapeModel(RefPtr<GPUDevice> gpu)
{
    //auto geometry = Shapes::Box(1, 1, 1);
    //auto geometry = Shapes::Ball(1, 10);
    //auto geometry = Shapes::Cylinder(1, 1, 10);
    //auto geometry = Shapes::Cone(1, 0.5f, 10);
    auto geometry = Shapes::Torus(1, 0.5, 5);
    const auto& [vertices, indices] = geometry;

    const MeshSpec meshSpecs[] =
    {
        {
            .IndexOffset = 0,
            .IndexCount = static_cast<uint32_t>(indices.size()),
            .MtlSpec =
            {
                .Color = {1, 0, 0},
                .VertexShader = "shaders/Debug/VertexShader",
                .FragmentShader = "shaders/Debug/FragmentShader",
                .Albedo = "images/Ant.png"
            }
        }
    };

    ModelSpec modelSpec
    {
        .Vertices = vertices,
        .Indices = indices,
        .MeshSpecs = meshSpecs
    };

    return gpu->CreateModel(modelSpec);
}

static Result<RefPtr<Model>> CreatePumpkinModel(RefPtr<GPUDevice> gpu)
{
    std::vector<Triangle> triangles;
    auto stlResult = loadAsciiSTL("models/Pumpkin-DD.stl", triangles);
    expect(stlResult, stlResult.error());

    std::map<TVertex, unsigned> vmap;
    std::vector<Vertex> vertices;
    std::vector<unsigned> indices;
    for (const auto& tri : triangles)
    {
        TVertex tverts[] =
        {
            //Change winding from CCW to CW
            tri.v[0], tri.v[2], tri.v[1]
        };

        for (auto& v : tverts)
        {
            //STL uses a right handed coordinate system with
            //Z up, Y into the screen, triangles winding counter clockwise.
            //This swap changes to a left handed coordinate system
            //with Y up, Z into the screen, and triangles winding clockwise.
            std::swap(v.pos.y, v.pos.z);
            std::swap(v.normal.y, v.normal.z);
        }

        for (int i = 0; i < 3; ++i)
        {
            const TVertex& tv = tverts[i];
            unsigned index;

            auto it = vmap.find(tv);
            if (vmap.end() == it)
            {
                index = vmap.size();
                vmap.emplace(tv, index);
                vertices.push_back(tv);
                vertices[index].normal = Vec3f(0, 0, 0);
            }
            else
            {
                index = it->second;
            }

            const Vec3f& v0 = tv.pos;
            const Vec3f& v1 = tverts[(i + 1) % 3].pos;
            const Vec3f& v2 = tverts[(i + 2) % 3].pos;

            Vec3 normal = (v1 - v0).Cross(v2 - v0).Normalize();
            vertices[index].normal = vertices[index].normal + normal;

            indices.push_back(index);
        }
    }

    for (auto& v : vertices)
    {
        v.normal = v.normal.Normalize();
    }

    MeshSpec meshSpecs[] =
    {
        {
            .IndexOffset = 0,
            .IndexCount = (unsigned)indices.size(),
            .MtlSpec =
            {
                .Color = {1, 0, 0},
                .VertexShader = "shaders/Debug/VertexShader",
                .FragmentShader = "shaders/Debug/ColorShader",
                .Albedo = "images/Ant.png"
            }
        },
    };

    ModelSpec modelSpec
    {
        .Vertices = vertices,
        .Indices = indices,
        .MeshSpecs = meshSpecs
    };

    return gpu->CreateModel(modelSpec);
}