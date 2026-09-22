#include "Camera.h"
#include "GpuHelper.h"
#include "ImGuiRenderer.h"
#include "Level.h"
#include "LevelDefs.h"
#include "Log.h"
#include "PerfMetrics.h"
#include "ResourceBundle.h"
#include "Scene.h"
#include "Shell.h"
#include "System.h"

#include <filesystem>
#include <imgui.h>
#include <optional>
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
            .BaseTexturePath{ "images/Ant.png" },
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
                    .Model = ModelRef{ .Name = "Triangle" },
                },
            },
        };

    outPropKitDef = std::move(propKitDef);
    outLevelDef = std::move(levelDef);

    return Result<>::Ok;
}

class TriangleApp : public ICoopTask<System&>
{
public:

    TriangleApp() = default;
    ~TriangleApp() override = default;
    TriangleApp(const TriangleApp&) = delete;
    TriangleApp& operator=(const TriangleApp&) = delete;
    TriangleApp(TriangleApp&&) = delete;
    TriangleApp& operator=(TriangleApp&&) = delete;

private:
    enum class Stage
    {
        None,
        CreatingScene,
        Running,
        Stopped
    };

    Result<> OnStart(System& system) override;

    void OnUpdate() override;

    Result<> RenderScene();

    System* m_System{ nullptr };

    PropKitDef m_PropKitDef;
    LevelDef m_LevelDef;

    std::optional<ResourceBundle> m_ResourceBundle;
    std::optional<Scene::CreateTask> m_SceneCreateTask;

    std::unique_ptr<Level> m_Level;
    std::unique_ptr<Scene> m_Scene;

    Viewport m_Viewport //
        {
            { .x = 0, .y = 0, .width = 1, .height = 1, .minDepth = 0, .maxDepth = 1 },
        };

    TrTransformf m_CameraXForm{ .T{ 0, 0, -4 } };

    Camera m_Camera{ m_Viewport };

    Stage m_Stage{ Stage::None };
};

Result<>
TriangleApp::OnStart(System& system)
{
    MLG_CHECKV(Stage::None == m_Stage, "Task has already been started");

    m_Stage = Stage::Stopped;

    m_System = &system;

    MLG_CHECK(CreateTriangleModel(m_PropKitDef, m_LevelDef));

    ResourceBundleBuilder builder;
    auto rsrcBundle = builder.Build(m_LevelDef, m_PropKitDef);
    MLG_CHECK(rsrcBundle, "Failed to build ResourceBundle");

    m_ResourceBundle = std::move(*rsrcBundle);

    auto levelResult = Level::Create(*m_ResourceBundle);
    MLG_CHECK(levelResult, "Failed to create Level");
    m_Level = std::move(*levelResult);

    const std::filesystem::path rootPath = ".";

    m_SceneCreateTask.emplace(*m_System, rootPath, *m_ResourceBundle, *m_Level);

    MLG_CHECK(m_SceneCreateTask->Start(), "Failed to begin scene create task");

    m_Viewport = Viewport(m_System->GetGpuHelper().GetScreenDimensions());
    m_Camera.SetViewport(m_Viewport);

    m_Stage = Stage::CreatingScene;

    return Result<>::Ok;
}

void
TriangleApp::OnUpdate()
{
    MLG_ASSERT(m_System);

    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::CreatingScene:
            MLG_ABORTIF(!m_SceneCreateTask, "Scene create task is not initialized");

            if(m_SceneCreateTask->IsRunning())
            {
                m_SceneCreateTask->Update();
            }
            else
            {
                auto sceneResult = m_SceneCreateTask->Take();
                if(MLG_VERIFY(sceneResult, "Failed to create Scene"))
                {
                    m_Scene = std::move(*sceneResult);
                    m_SceneCreateTask.reset();
                    m_ResourceBundle.reset();
                    m_Stage = TriangleApp::Stage::Running;
                }
                else
                {
                    m_Stage = Stage::Stopped;
                }
            }
            break;

        case Stage::Running:
            if(m_System->ShouldQuit())
            {
                m_Stage = Stage::Stopped;
            }
            else if(!m_System->IsMinimized())
            {
                if(!MLG_VERIFY(RenderScene(), "Failed to render Scene"))
                {
                    m_Stage = Stage::Stopped;
                }
            }
            break;

        case Stage::Stopped:
            SetComplete();
            break;
    }
}

Result<>
TriangleApp::RenderScene()
{
    MLG_ASSERT(m_System);

    const GpuHelper& gpuHelper = m_System->GetGpuHelper();

    m_Viewport = Viewport(gpuHelper.GetScreenDimensions());
    m_Camera.SetViewport(m_Viewport);

    MLG_CHECK(m_Scene->Render(m_Camera, m_CameraXForm), "Failed to render Scene");

    auto target = gpuHelper.GetSwapChainTexture();
    MLG_CHECK(target, "Failed to get swap chain texture");

    MLG_CHECK(m_Scene->Composite(*target), "Failed to composite Scene");

    MLG_CHECK(m_System->GetImGuiRenderer().Render(gpuHelper.GetDevice(), *target, RenderGui),
        "Failed to render ImGui");

    return Result<>::Ok;
}

void
Run()
{
    static TriangleApp triangleApp;
    static Shell shell(kAppName, triangleApp);
    static bool started = false;

    if(!started)
    {
        started = true;
        if(!shell.Start())
        {
            MLG_ERROR("Failed to start Shell");
            return;
        }
    }

    if(shell.IsRunning())
    {
        shell.Update();
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
    Log::SetLevel(Log::Level::Trace);

    emscripten_set_main_loop(Run, 0, 1);

    return 0;
}