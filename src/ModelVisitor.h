#pragma once

#include <stack>

#include "RenderGraph.h"
#include "SceneVisitor.h"
#include "VecMath.h"
#include "RefCount.h"

class ModelVisitor : public SceneVisitor
{
public:

    ModelVisitor() = delete;

    explicit ModelVisitor(RefPtr<RenderGraph> renderGraph)
        : m_RenderGraph(renderGraph)
    {
        m_TransformStack.push(Mat44f::Identity());
    }

    ~ModelVisitor() override {}

    void Visit(Model* node) override;

    void Visit(GroupNode* node) override;

    void Visit(TransformNode* node) override;

private:

    RefPtr<RenderGraph> m_RenderGraph;

    std::stack<Mat44f> m_TransformStack;

    IMPLEMENT_NON_COPYABLE(ModelVisitor);
};