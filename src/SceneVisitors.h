#pragma once

#include "RefCount.h"
#include "VecMath.h"

#include <stack>
#include <list>

class ModelNode;
class CameraNode;
class GroupNode;
class TransformNode;
class RenderGraph;

class SceneVisitor
{
public:

    SceneVisitor() = default;

    virtual ~SceneVisitor() = 0 {}

    virtual void Visit(ModelNode* node) {}
    virtual void Visit(CameraNode* node) {}
    virtual void Visit(TransformNode* node) {}
    virtual void Visit(GroupNode* node) {}
};

class GroupVisitor : public SceneVisitor
{
public:

    void Visit(GroupNode* node) override;

protected:

    GroupVisitor() = default;

private:

    IMPLEMENT_NON_COPYABLE(GroupVisitor);
};

class TransformVisitor : public GroupVisitor
{
public:

    TransformVisitor()
    {
        m_TransformStack.push(Mat44f::Identity());
    }

    ~TransformVisitor() override {}

    void Visit(TransformNode* node) override;

    const Mat44f& GetTransform() const
    {
        return m_TransformStack.top();
    }

private:

    std::stack<Mat44f> m_TransformStack;

    IMPLEMENT_NON_COPYABLE(TransformVisitor);
};

class ModelVisitor : public TransformVisitor
{
public:

    ModelVisitor() = delete;

    explicit ModelVisitor(RenderGraph* renderGraph);

    ~ModelVisitor() override {}

    void Visit(ModelNode* node) override;

private:

    RenderGraph* const m_RenderGraph;

    IMPLEMENT_NON_COPYABLE(ModelVisitor);
};

class CameraVisitor : public TransformVisitor
{
public:

    struct ViewspaceCamera
    {
        const Mat44f Transform;
        const Mat44f& Projection;
    };

    using CameraList = std::list<ViewspaceCamera>;

    CameraVisitor() = default;

    ~CameraVisitor() override {}

    void Visit(CameraNode* node) override;

    CameraList GetCameras()
    {
        return m_CameraList;
    }

    const CameraList GetCameras() const
    {
        return m_CameraList;
    }

private:

    CameraList m_CameraList;

    IMPLEMENT_NON_COPYABLE(CameraVisitor);
};