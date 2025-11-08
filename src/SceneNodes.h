#pragma once

#include "Model.h"

#include <deque>
#include <span>

class SceneVisitor;

class SceneNode
{
public:

    virtual ~SceneNode() = 0 {}

    virtual void PreAccept(SceneVisitor* visitor) {}

    virtual void Accept(SceneVisitor* visitor) = 0;

    virtual void PostAccept(SceneVisitor* visitor) {}

protected:

    SceneNode() = default;

private:

    IMPLEMENT_REFCOUNT(SceneNode);
};

class GroupNode : public SceneNode
{
public:

    static Result<RefPtr<GroupNode>> Create();

    ~GroupNode() override {}

    virtual void Accept(SceneVisitor* visitor) override;

    void AddChild(RefPtr<SceneNode> child);

    void RemoveChild(RefPtr<SceneNode> child);

    using iterator = std::deque<RefPtr<SceneNode>>::iterator;
    using const_iterator = std::deque<RefPtr<SceneNode>>::const_iterator;

    iterator begin() { return m_Children.begin(); }
    const_iterator begin()const { return m_Children.begin(); }
    iterator end() { return m_Children.end(); }
    const_iterator end()const { return m_Children.end(); }

protected:

    GroupNode() = default;

private:

    std::deque<RefPtr<SceneNode>> m_Children;
};

class TransformNode : public GroupNode
{
public:

    ~TransformNode() override {}

    void PreAccept(SceneVisitor* visitor) override;

    void Accept(SceneVisitor* visitor) override;

    void PostAccept(SceneVisitor* visitor) override;

    Mat44f Transform{ 1 };

protected:

    TransformNode() = default;
};

class CameraNode : public TransformNode
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

class ModelNode : public TransformNode
{
public:

    static Result<RefPtr<ModelNode>> Create(RefPtr<Model> model);

    void Accept(SceneVisitor* visitor) override;

    RefPtr<Model> const Model;

private:

    ModelNode() = delete;

    ModelNode(RefPtr<::Model> model);
};