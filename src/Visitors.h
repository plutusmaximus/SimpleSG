#pragma once

#include <stack>
#include <list>

#include "RenderGraph.h"
#include "SceneVisitor.h"
#include "VecMath.h"

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

    explicit ModelVisitor(RefPtr<RenderGraph> renderGraph)
        : m_RenderGraph(renderGraph)
    {
    }

    ~ModelVisitor() override {}

    void Visit(ModelNode* node) override;

private:

    RefPtr<RenderGraph> m_RenderGraph;

    IMPLEMENT_NON_COPYABLE(ModelVisitor);
};

class CameraVisitor : public TransformVisitor
{
public:

    struct ViewspaceCamera
    {
        const Mat44f ViewTransform;
        RefPtr<CameraNode> Camera;
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