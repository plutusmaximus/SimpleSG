#include "Camera.h"
#include "FileFetcher.h"
#include "GpuHelper.h"
#include "ImGuiRenderer.h"
#include "Level.h"
#include "Log.h"
#include "PerfMetrics.h"
#include "PropKit.h"
#include "Renderer.h"
#include "Scene.h"
#include "System.h"
#include "ThreadPool.h"

#include <filesystem>
#include <imgui.h>
#include <SDL3/SDL_events.h>

namespace
{
constexpr const char* kAppName = "EmTriangle";

Result<>
RenderGui()
{
    ImGui::SetNextWindowSize(ImVec2(0, 0)); // Auto-fit both width and height
    ImGui::Begin("Counters");

    constexpr size_t kMaxPerfStats = 256;

    PerfStats perfStats[kMaxPerfStats];
    std::span<PerfStats> perfStatsSpan(perfStats);
    const size_t counterCount = PerfMetrics::SampleCounters(perfStatsSpan);
    for(const auto& counterStat : perfStatsSpan.first(counterCount))
    {
        const std::string text =
            std::format("{}: {:.3f} ms", counterStat.GetName(), counterStat.GetEMA());
        ImGui::Text("%s", text.c_str()); // NOLINT(cppcoreguidelines-pro-type-vararg)
    }

    ImGui::End();

    return Result<>::Ok;
}

Result<>
CreateTriangleModel(PropKitDef& outPropKitDef, LevelDef& outLevelDef)
{
    std::vector<Vertex> triangleVertices = //
        {
            { .pos{ 0.0f, 0.5f, 0.0f },
                .normal{ 0.0f, 0.0f, -1.0f },
                .uvs{ { .u = 1, .v = 1 } } }, // 0
            { .pos{ 0.5f, 0.0f, 0.0f },
                .normal{ 0.0f, 0.0f, -1.0f },
                .uvs{ { .u = 0, .v = 1 } } }, // 1
            { .pos{ -0.5f, 0.0f, 0.0f },
                .normal{ 0.0f, 0.0f, -1.0f },
                .uvs{ { .u = 0, .v = 0 } } }, // 2
        };

    std::vector<VertexIndex> triangleIndices = { 0, 1, 2 };

    MaterialDef mtlDef //
        {
            .BaseTextureUri{ "images/Ant.png" },
            .Color{ "#FFA500"_rgba },
            .Metalness = 0,
            .Roughness = 0,
        };

    MeshDef meshDef //
        {
            .Vertices{ std::move(triangleVertices) },
            .Indices{ std::move(triangleIndices) },
            .MaterialDef{ std::move(mtlDef) },
        };

    ModelDef modelDef //
        {
            .Name{ "Triangle" },
            .MeshDefs{ std::move(meshDef) },
        };

    PropKitDef propKitDef //
        {
            .ModelDefs{ std::move(modelDef) },
        };

    LevelDef levelDef //
        {
            .NodeDefs //
            {
                {
                    .Name{ "TriangleNode" },
                    .Transform{},
                    .Components //
                    {
                        .Model = ModelRef{ .Name = "Triangle" },
                    },
                },
            },
        };

    outPropKitDef = std::move(propKitDef);
    outLevelDef = std::move(levelDef);

    return Result<>::Ok;
}

class TriangleApp
{
public:
    static Result<> MainLoop(System& system, TriangleApp* self);

    Result<> Update(System& system);

private:
    enum class State
    {
        Init,
        Running,
        Shutdown,
        Stopped
    };

    PropKitDef m_PropKitDef;
    LevelDef m_LevelDef;

    Result<PropKit> m_PropKit;
    Result<Level> m_Level;
    Result<Scene> m_Scene;

    Viewport m_Viewport //
        {
            { .x = 0, .y = 0, .width = 1, .height = 1, .minDepth = 0, .maxDepth = 1 },
        };

    TrTransformf m_CameraXForm{ .T{ 0, 0, -4 } };

    Camera m_Camera{ m_Viewport };

    State m_State{ State::Init };
};

Result<>
TriangleApp::MainLoop(System& system, TriangleApp* self)
{
    return self->Update(system);
}

Result<>
TriangleApp::Update(System& system)
{
    // If an error occurs that causes an early exit this will run and set the state to Shutdown.
    MLG_DEFER_AS(shutdownOnExit)
    {
        System::PostQuitEvent();
        m_State = TriangleApp::State::Shutdown;
    };

    switch(m_State)
    {
        case TriangleApp::State::Init:
        {
            MLG_CHECK(CreateTriangleModel(m_PropKitDef, m_LevelDef));

            GpuHelper& gpuHelper = system.GetGpuHelper();
            ThreadPool& threadPool = system.GetThreadPool();
            FileFetcher& fileFetcher = system.GetFileFetcher();

            const std::filesystem::path rootPath = ".";
            m_PropKit = PropKit::Create(gpuHelper, threadPool, fileFetcher, rootPath, m_PropKitDef);
            MLG_CHECK(m_PropKit, "Failed to create PropKit");

            m_Level = Level::Create(m_LevelDef, *m_PropKit);
            MLG_CHECK(m_Level, "Failed to create Level");

            m_Scene = Scene::Create(gpuHelper, *m_Level);
            MLG_CHECK(m_Scene, "Failed to create Scene");

            m_Viewport = Viewport(gpuHelper.GetScreenDimensions());
            m_Camera.SetViewport(m_Viewport);

            m_State = TriangleApp::State::Running;
        }
        break;

        case TriangleApp::State::Running:
            if(!system.IsMinimized())
            {
                const GpuHelper& gpuHelper = system.GetGpuHelper();
                Renderer& renderer = system.GetRenderer();

                m_Viewport = Viewport(gpuHelper.GetScreenDimensions());
                m_Camera.SetViewport(m_Viewport);

                MLG_CHECK(
                    renderer.Render(m_Camera, m_CameraXForm, *m_Scene, *m_PropKit));

                auto target = gpuHelper.GetSwapChainTexture();
                MLG_CHECK(target, "Failed to get swapchain texture");

                MLG_CHECK(renderer.Composite(*target));

                MLG_CHECK(
                    system.GetImGuiRenderer().Render(gpuHelper.GetDevice(), *target, RenderGui));
            }
            break;

        case TriangleApp::State::Shutdown:
            System::PostQuitEvent();
            m_State = TriangleApp::State::Stopped;
            break;

        case TriangleApp::State::Stopped:
            break;
    }

    // We're returning successfully - cancel the shutdownOnExit.
    shutdownOnExit.release();

    return Result<>::Ok;
}

struct Shell
{
    enum class State
    {
        Init,
        CreatingSystem,
        Running,
        Shutdown,
        Stopped
    };

    class MainLoopHandler
    {
    public:
        MainLoopHandler() = delete;
        ~MainLoopHandler() = default;
        MainLoopHandler(const MainLoopHandler&) = delete;
        MainLoopHandler& operator=(const MainLoopHandler&) = delete;
        MainLoopHandler(MainLoopHandler&& other)
            : m_Invoke(other.m_Invoke),
              m_Cb(other.m_Cb),
              m_UserData(other.m_UserData)
        {
            other.m_Invoke = nullptr;
            other.m_Cb = nullptr;
            other.m_UserData = nullptr;
        }
        MainLoopHandler& operator=(MainLoopHandler&& other)
        {
            if(this != &other)
            {
                m_Invoke = other.m_Invoke;
                m_Cb = other.m_Cb;
                m_UserData = other.m_UserData;

                other.m_Invoke = nullptr;
                other.m_Cb = nullptr;
                other.m_UserData = nullptr;
            }

            return *this;
        }

        // NOLINTBEGIN
        template<typename T>
        MainLoopHandler(Result<> (*func)(System& system, T* userData), T* userData)
            : m_Invoke(&MainLoopHandler::InvokeImpl<T>),
              m_Cb(reinterpret_cast<Result<> (*)(System&, void*)>(func)),
              m_UserData(userData)
        {
        }
        // NOLINTEND

        Result<> operator()(System& system) const { return (this->*m_Invoke)(system); }

    private:
        // NOLINTBEGIN
        template<typename T>
        Result<> InvokeImpl(System& system) const
        {
            auto cb = reinterpret_cast<Result<> (*)(System&, T*)>(m_Cb);
            return cb(system, reinterpret_cast<T*>(m_UserData));
        }
        // NOLINTEND

        Result<> (MainLoopHandler::*m_Invoke)(System& system) const = nullptr;
        Result<> (*m_Cb)(System&, void*) = nullptr;
        void* m_UserData = nullptr;
    };

    /// Main entry point for the application.  Used to call the main loop from Emscripten or other
    /// platforms that require a static entry point.
    static void Main(Shell* shell);

    /// Handles system level tasks.  Calls the application main loop handler when the system is
    /// running.
    Result<> InnerMain();

    bool IsStopped() const { return State::Stopped == m_State; }

    Result<System::CreateTask> SystemCreateTask;
    Result<System> SystemInstance;

    MainLoopHandler mainLoopHandler;

    State m_State{ State::Init };
};

void
Shell::Main(Shell* shell)
{
    Result<> result = Result<>::Fail;

    if(MLG_VERIFY(shell, "Shell is null"))
    {
        result = shell->InnerMain();
    }

    if(!result && !shell->IsStopped())
    {
        MLG_ERROR("Shell::Main failed:");
        shell->m_State = State::Shutdown;
    }
}

Result<>
Shell::InnerMain()
{
    // If an error occurs that results in an early exit then this
    // will run and set the state to Shutdown.
    MLG_DEFER_AS(shutdownOnExit)
    {
        m_State = State::Shutdown;
    };

    switch(m_State)
    {
        case State::Init:
        {
            Log::SetLevel(Log::Level::Trace);

            auto cwd = std::filesystem::current_path();
            MLG_INFO("Current working directory: {}", cwd.string());

            SystemCreateTask = System::Create(kAppName);
            MLG_CHECK(SystemCreateTask, "Failed to create System");
            m_State = State::CreatingSystem;
        }
        break;

        case State::CreatingSystem:
            MLG_CHECK(SystemCreateTask->Update());

            if(SystemCreateTask->IsComplete())
            {
                MLG_CHECK(SystemCreateTask->Succeeded(), "System creation failed");
                SystemInstance = SystemCreateTask->Get();
                MLG_CHECK(SystemInstance, "Failed to get System instance");

                m_State = State::Running;
            }
            break;

        case State::Running:
        {
            MLG_SCOPED_TIMER("Frame");

            SystemInstance->ProcessEvents();

            if(SystemInstance->ShouldQuit())
            {
                m_State = State::Shutdown;
            }
            else
            {
                MLG_CHECK(mainLoopHandler(*SystemInstance), "MainLoop failed");

#if !defined(__EMSCRIPTEN__)

                const GpuHelper& gpuHelper = SystemInstance->GetGpuHelper();

                MLG_CHECK(gpuHelper.GetSurface().Present(), "Failed to present backbuffer");
                gpuHelper.GetInstance().ProcessEvents();
#endif
            }
        }
        break;

        case State::Shutdown:
            PerfMetrics::LogCounters();
            m_State = State::Stopped;
            break;

        case State::Stopped:
            // emscripten_cancel_main_loop();
            break;
    }

    // We're returning successfully - cancel the shutdownOnExit.
    shutdownOnExit.release();

    return Result<>::Ok;
}
} // namespace

int
main(int /*argc*/, char** /*argv*/)
{
    static TriangleApp triangleApp;
    ;
    static Shell shellContext //
        {
            .mainLoopHandler = Shell::MainLoopHandler(TriangleApp::MainLoop, &triangleApp),
        };

    while(!shellContext.IsStopped())
    {
        Shell::Main(&shellContext);
    }

    // emscripten_set_main_loop_arg(Shell::Main, &shellContext, 0, 1);

    return 0;
}