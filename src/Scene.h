#pragma once

#include "BoundedVector.h"
#include "Result.h"
#include "SceneTypes.h"

#include <memory>
#include <span>

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

    void Update(const float timeStep);

    void SetActive(const SceneNode& node, bool active);

    void SetVisible(const SceneNode& node, bool visible);

private:
    Scene(BoundedVector<SceneNode>&& nodes,
        BoundedVector<PhysicsNode>&& physicsNodes,
        BoundedVector<ModelNode>&& modelNodes,
        BoundedVector<MeshInstance>&& meshInstances,
        const WorldIdentifier worldId);

    SceneNode* GetMutableNode(const SceneNode& node);

    void UpdateWorldTransforms();

    BoundedVector<SceneNode> m_Nodes;
    BoundedVector<PhysicsNode> m_PhysicsNodes;
    BoundedVector<ModelNode> m_ModelNodes;
    BoundedVector<MeshInstance> m_MeshInstances;
    std::span<SceneNode> m_RootNodes;
    WorldIdentifier m_WorldId;
};