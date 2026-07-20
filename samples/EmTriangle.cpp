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
#include "Shell.h"
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

    /// @brief Called by the Shell.  Calls InnerUpdate to perform the main work of the application,
    /// and handles any errors that occur.
    Shell::AppState Update(System& system);

private:
    enum class State
    {
        Init,
        Running,
        Shutdown,
        Stopped
    };

    /// @brief Performs the the main work of the application.
    Result<> InnerUpdate(System& system);

    Shell::AppState GetAppState() const
    {
        return State::Stopped == m_State ? Shell::AppState::Stopped : Shell::AppState::Running;
    }

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
    
Shell::AppState
TriangleApp::Update(System& system)
{
    const Result<> result = InnerUpdate(system);

    if(!result)
    {
        MLG_ERROR("TriangleApp::Update failed: {}");
        m_State = State::Shutdown;
    }

    return GetAppState();
}

Result<>
TriangleApp::InnerUpdate(System& system)
{
    switch(m_State)
    {
        case State::Init:
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

        case State::Running:
            if(system.ShouldQuit())
            {
                m_State = State::Shutdown;
            }
            else if(!system.IsMinimized())
            {
                const GpuHelper& gpuHelper = system.GetGpuHelper();
                Renderer& renderer = system.GetRenderer();

                m_Viewport = Viewport(gpuHelper.GetScreenDimensions());
                m_Camera.SetViewport(m_Viewport);

                MLG_CHECK(renderer.Render(m_Camera, m_CameraXForm, *m_Scene, *m_PropKit));

                auto target = gpuHelper.GetSwapChainTexture();
                MLG_CHECKV(target, "Failed to get swap chain texture");

                MLG_CHECK(renderer.Composite(*target));

                MLG_CHECK(
                    system.GetImGuiRenderer().Render(gpuHelper.GetDevice(), *target, RenderGui));
            }
            break;

        case State::Shutdown:
            m_State = State::Stopped;
            break;

        case State::Stopped:
            break;
    }

    return Result<>::Ok;
}

void
Run()
{
    static Shell shell(kAppName);
    static TriangleApp triangleApp;

    static auto AppUpdate = [](System& system)
    {
        return triangleApp.Update(system);
    };

    if(!shell.IsStopped())
    {
        const Result<> result = shell.Update(+AppUpdate);

        if(!result)
        {
            MLG_ERROR("Shell::Main failed:");
            shell.Shutdown();
        }
    }
    else
    {
        emscripten_cancel_main_loop();
    }
}

} // namespace

int
main(int /*argc*/, char** /*argv*/)
{
    emscripten_set_main_loop(Run, 0, 1);

    return 0;
}