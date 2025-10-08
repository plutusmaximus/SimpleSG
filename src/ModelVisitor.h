#pragma once

#include <stack>

#include "SceneVisitor.h"
#include "VecMath.h"
#include "RefCount.h"
#include "Camera.h"

class RenderGraph;

class ModelVisitor : public SceneVisitor
{
public:

    ModelVisitor() = delete;

    ModelVisitor(RenderGraph* renderGraph, const Camera& camera)
        : m_RenderGraph(renderGraph)
        , m_Camera(camera)
    {
        m_TransformStack.push(Mat44f::Identity());
    }

    ~ModelVisitor() override {}

    void Visit(ModelNode* node) override;

    void Visit(GroupNode* node) override;

    void Visit(TransformNode* node) override;

    const Mat44f& GetTransform()
    {
        return m_TransformStack.top();
    }

private:

    Camera m_Camera;
    RenderGraph* m_RenderGraph;

    std::stack<Mat44f> m_TransformStack;

    IMPLEMENT_NON_COPYABLE(ModelVisitor);
};