#pragma once

#include "CoopTask.h"
#include "FilePath.h"
#include "ResourceBundle.h"
#include "SceneTypes.h"
#include "View.h"

#include <memory>
#include <optional>
#include <variant>

class Scene;
class System;

/// Represents a level in the game, containing a scene and a view.
/// Use the CreateTask class to asynchronously create a Level by loading
/// a scene definition, a resource bundle file, or a glTF file.
class Level
{
public:
    class CreateTask;

    Level() = delete;
    ~Level();
    Level(const Level&) = delete;
    Level& operator=(const Level&) = delete;
    Level(Level&&) = delete;
    Level& operator=(Level&&) = delete;

    Scene& GetScene() const { return *m_Scene; }
    View& GetView() const { return *m_View; }

private:
    friend class CreateTask;

    Level(std::unique_ptr<Scene>&& scene, std::unique_ptr<View>&& view);

    std::unique_ptr<Scene> m_Scene;
    std::unique_ptr<View> m_View;
};

class Level::CreateTask : public ICoopTask<>
{
public:
    struct BundleParams
    {
        FilePath Path;
    };

    struct GltfParams
    {
        FilePath Path;
    };

    CreateTask() = delete;
    ~CreateTask() override;
    CreateTask(const CreateTask&) = delete;
    CreateTask& operator=(const CreateTask&) = delete;
    CreateTask(CreateTask&&) = delete;
    CreateTask& operator=(CreateTask&&) = delete;

    CreateTask(System& system, SceneDef&& sceneDef);

    CreateTask(System& system, BundleParams bundleParams);

    CreateTask(System& system, GltfParams gltfParams);

    Result<std::unique_ptr<Level>> Take();

private:
    enum class Stage
    {
        None,
        StartFromSceneDef,
        StartFromBundleParams,
        StartFromGltfParams,
        LoadingBundle,
        CreatingView,
        Succeeded,
        Failed
    };

    Result<> OnStart() override;

    void OnUpdate() override;

    Result<FilePath> GetPath() const;

    static Result<ResourceBundle> CreateResourceBundle(const SceneDef& sceneDef);

    static Result<std::unique_ptr<Scene>> CreateScene(const ResourceBundle& resourceBundle);

    Result<> CreateView(const DirectoryPath& parentPath, const ResourceBundle& resourceBundle, Scene& scene);

    System* m_System{ nullptr };

    std::variant<SceneDef, GltfParams, BundleParams> m_Params;

    std::optional<ResourceBundle::LoadTask> m_RsrcBundleLoadTask;

    std::optional<View::CreateTask> m_CreateViewTask;

    ResourceBundle m_ResourceBundle;
    std::unique_ptr<Scene> m_Scene;
    std::unique_ptr<Level> m_Level;

    Stage m_Stage{ Stage::None };
};