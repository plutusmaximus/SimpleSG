#pragma once

#include "Result.h"
#include "SceneTypes.h"

#include <memory>
#include <span>
#include <vector>

class ResourceBundle;

class Scene
{
public:
    static Result<std::unique_ptr<Scene>> Create(const ResourceBundle& resourceBundle);

    Scene() = delete;
    ~Scene();
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) = delete;
    Scene& operator=(Scene&&) = delete;

    /// Returns all nodes in the scene, in breadth-first order.
    std::span<const SceneNode> GetAllNodes() const { return m_Nodes; }

    /// Returns all physics nodes in the scene, in breadth-first order.
    std::span<const PhysicsNode> GetAllPhysicsNodes() const { return m_PhysicsNodes; }
    std::span<PhysicsNode> GetAllPhysicsNodes() { return m_PhysicsNodes; }

    /// Returns all model nodes in the scene, in breadth-first order.
    std::span<const ModelNode> GetAllModelNodes() const { return m_ModelNodes; }
    std::span<ModelNode> GetAllModelNodes() { return m_ModelNodes; }

    /// Returns the root nodes of the scene. Root nodes are nodes that have no parent.
    std::span<const SceneNode> GetRoots() const { return m_RootNodes; }

    void Update(const float timeStep);

    void SetActive(const SceneNode& node, bool active);

    void SetVisible(const SceneNode& node, bool visible);

private:
    Scene(std::vector<SceneNode>&& nodes,
        std::vector<PhysicsNode>&& physicsNodes,
        std::vector<ModelNode>&& modelNodes,
        std::vector<MeshInstance>&& meshInstances,
        const WorldIdentifier worldId);

    SceneNode* GetNode(const SceneNode& node);

    void UpdateWorldTransforms(std::span<SceneNode> nodes);

    std::vector<SceneNode> m_Nodes;
    std::vector<PhysicsNode> m_PhysicsNodes;
    std::vector<ModelNode> m_ModelNodes;
    std::vector<MeshInstance> m_MeshInstances;
    std::span<SceneNode> m_RootNodes;
    WorldIdentifier m_WorldId;
};