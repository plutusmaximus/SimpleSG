#include "Level.h"

#include "GltfLoader.h"
#include "ResourceBundle.h"
#include "Scene.h"

Level::Level(std::unique_ptr<Scene>&& scene, std::unique_ptr<View>&& view)
    : m_Scene(std::move(scene)),
      m_View(std::move(view))
{
    MLG_ABORTIF(!m_Scene || !m_View, "Scene or View is null");
}

Level::CreateTask::CreateTask(System& system, SceneDef&& sceneDef)
    : m_System(&system),
      m_Params(std::move(sceneDef)),
      m_Stage(Stage::StartFromSceneDef)
{
}

Level::CreateTask::CreateTask(System& system, BundleParams bundleParams)
    : m_System(&system),
      m_Params(std::move(bundleParams)),
      m_Stage(Stage::StartFromBundleParams)
{
}

Level::CreateTask::CreateTask(System& system, GltfParams gltfParams)
    : m_System(&system),
      m_Params(std::move(gltfParams)),
      m_Stage(Stage::StartFromGltfParams)
{
}

Result<std::unique_ptr<Level>>
Level::CreateTask::Take()
{
    MLG_CHECK(m_Stage != Stage::None, "Task has not started");
    MLG_CHECK(!IsRunning(), "Task is still running");
    MLG_CHECK(Stage::Succeeded == m_Stage, "Task has not succeeded");
    MLG_CHECKV(m_Level, "Level has already been taken");

    auto bye = std::move(m_Level);
    m_Level.reset();

    return std::move(bye);
}

Result<>
Level::CreateTask::OnStart()
{
    MLG_CHECKV(Stage::StartFromSceneDef == m_Stage
            || Stage::StartFromBundleParams == m_Stage
            || Stage::StartFromGltfParams == m_Stage,
        "Invalid initial stage");

    return Result<>::Ok;
}

void
Level::CreateTask::OnUpdate()
{
    switch(m_Stage)
    {
        case Stage::None:
            MLG_ABORT("Task is not running");
            break;

        case Stage::StartFromSceneDef:
            if(!CreateFromSceneDef(std::get<SceneDef>(m_Params)))
            {
                MLG_ERROR("Failed to create from SceneDef");
                m_Stage = Stage::Failed;
            }
            else
            {
                const DirectoryPath parentPath = DirectoryPath::Current();
                m_OptCreateViewTask.emplace(*m_System, parentPath, *m_RsrcBundle, *m_Scene);
                if(!m_OptCreateViewTask->Start())
                {
                    MLG_ERROR("Failed to start view creation task");
                    m_Stage = Stage::Failed;
                }
                else
                {
                    m_Stage = Stage::CreatingView;
                }
            }
            break;

        case Stage::StartFromBundleParams:
            MLG_ERROR("Loading from bundle is not implemented");
            m_Stage = Stage::Failed;
            break;
        case Stage::StartFromGltfParams:
        {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-union-access)
            const std::string_view path = std::get<GltfParams>(m_Params).Path;

            if(const auto loadResult = GltfLoader::Load(path); !loadResult)
            {
                MLG_ERROR("Failed to load glTF file: {}", path);
                m_Stage = Stage::Failed;
            }
            else if(!CreateFromSceneDef(*loadResult))
            {
                MLG_ERROR("Failed to create from SceneDef");
                m_Stage = Stage::Failed;
            }
            else if(const auto parentPath = DirectoryPath::ParentPath(path); !parentPath)
            {
                MLG_ERROR("Failed to get parent path for {}", path);
                m_Stage = Stage::Failed;
            }
            else
            {
                m_OptCreateViewTask.emplace(*m_System, *parentPath, *m_RsrcBundle, *m_Scene);
                if(!m_OptCreateViewTask->Start())
                {
                    MLG_ERROR("Failed to start view creation task");
                    m_Stage = Stage::Failed;
                }
                else
                {
                    m_Stage = Stage::CreatingView;
                }
            }
            break;
        }
        case Stage::CreatingView:
            if(!MLG_VERIFY(m_OptCreateViewTask, "View creation task is not valid"))
            {
                m_Stage = Stage::Failed;
            }
            else if(m_OptCreateViewTask->IsRunning())
            {
                m_OptCreateViewTask->Update();
            }
            else
            {
                auto viewResult = m_OptCreateViewTask->Take();

                if(viewResult)
                {
                    m_Level = std::unique_ptr<Level>(
                        new Level(std::move(m_Scene), std::move(*viewResult)));
                    m_Stage = Stage::Succeeded;
                }
                else
                {
                    MLG_ERROR("Failed to take view from creation task");
                    m_Stage = Stage::Failed;
                }
            }
            break;

        case Stage::Failed:
            MLG_ERROR("Task failed");
            [[fallthrough]];
        case Stage::Succeeded:
            {
                auto bye1 = std::move(m_RsrcBundle);
                m_OptCreateViewTask.reset();
            }
            SetComplete();
            break;
    }
}

Result<>
Level::CreateTask::CreateFromSceneDef(const SceneDef& sceneDef)
{
    ResourceBundleBuilder builder;
    auto buldResult = builder.Build(sceneDef);
    MLG_CHECK(buldResult, "Failed to build ResourceBundle from SceneDef");
    m_RsrcBundle = std::move(*buldResult);

    auto sceneResult = Scene::Create(*m_RsrcBundle);

    MLG_CHECK(sceneResult, "Failed to create scene from resource bundle");

    m_Scene = std::move(*sceneResult);

    return Result<>::Ok;
}