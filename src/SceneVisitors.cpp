#include "SceneVisitors.h"

#include "SceneNodes.h"
#include "RenderGraph.h"

void GroupVisitor::Visit(GroupNode* node)
{
    for (auto child : *node)
    {
        child->Accept(this);
    }
}

void TransformVisitor::Visit(TransformNode* node)
{
    m_TransformStack.push(GetTransform().Mul(node->Transform.ToMatrix()));

    this->GroupVisitor::Visit(node);

    m_TransformStack.pop();
}

ModelVisitor::ModelVisitor(RenderGraph* renderGraph)
    : m_RenderGraph(renderGraph)
{
}

void ModelVisitor::Visit(ModelNode* node)
{
    m_RenderGraph->Add(GetTransform(), node->Model);
}

void CameraVisitor::Visit(CameraNode* node)
{
    m_CameraList.emplace_back(ViewspaceCamera
        {
            .Transform = GetTransform(),
            .Projection = node->GetProjection()
        });
}   