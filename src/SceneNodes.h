#pragma once

#include "RefCount.h"
#include "Mesh.h"
#include "Vertex.h"

#include <vector>
#include <span>

class SceneVisitor;

class SceneNode
{
public:

    SceneNode() = default;

    virtual ~SceneNode() = 0 {}

    virtual void Accept(SceneVisitor* visitor) = 0;

private:

    IMPLEMENT_REFCOUNT(SceneNode);
};

class GroupNode : public SceneNode
{
public:

    GroupNode() = default;

    ~GroupNode() override {}

    virtual void Accept(SceneVisitor* visitor) override;

    void Traverse(SceneVisitor* visitor);

    void AddChild(RefPtr<SceneNode> child);

    void RemoveChild(RefPtr<SceneNode> child);

    using iterator = std::vector<RefPtr<SceneNode>>::iterator;
    using const_iterator = std::vector<RefPtr<SceneNode>>::const_iterator;

    iterator begin() { return m_Children.begin(); }
    const_iterator begin()const { return m_Children.begin(); }
    iterator end() { return m_Children.end(); }
    const_iterator end()const { return m_Children.end(); }

private:

    std::vector<RefPtr<SceneNode>> m_Children;
};

class TransformNode : public GroupNode
{
public:

    TransformNode();

    ~TransformNode() override {}

    void Accept(SceneVisitor* visitor) override;

    Mat44f Transform;
};

class CameraNode : public SceneNode
{
public:

    static Result<RefPtr<CameraNode>> Create();

    ~CameraNode() override {}

    void Accept(SceneVisitor* visitor) override;

    void SetPerspective(const Degreesf fov, const float width, const float height, const float nearClip, const float farClip);

    void SetBounds(const float width, const float height);

    const Mat44f& GetProjection() const;

private:

    CameraNode() = default;

    Degreesf m_Fov{ 0 };
    float m_Width{ 0 }, m_Height{ 0 };
    float m_Near{ 0 };
    float m_Far{ 0 };
    Mat44f m_Proj{ 1 };
};

class ModelSpec
{
public:
    const std::span<const Vertex> Vertices;
    const std::span<const VertexIndex> Indices;
    const std::span<const MeshSpec> MeshSpecs;
};

class ModelNode : public SceneNode
{
public:

    class Meshes : private std::vector<RefPtr<Mesh>>
    {
        friend class ModelNode;
    public:

        using iterator = std::vector<RefPtr<Mesh>>::iterator;
        using const_iterator = std::vector<RefPtr<Mesh>>::const_iterator;

        using std::vector<RefPtr<Mesh>>::begin;
        using std::vector<RefPtr<Mesh>>::end;
    };

    static Result<RefPtr<ModelNode>> Create(std::span<RefPtr<Mesh>> meshes);

    void Accept(SceneVisitor* visitor) override;

    Meshes Meshes;

private:

    ModelNode() = delete;

    ModelNode(std::span<RefPtr<Mesh>> meshes);
};