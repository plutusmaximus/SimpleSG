#include "SceneVisitors.h"

#include "SceneNodes.h"
#include "RenderGraph.h"

void GroupVisitor::Visit(GroupNode* node)
{
    for (auto child : *node)
    {
        child->PreAccept(this);
        child->Accept(this);
        child->PostAccept(this);
    }
}

void TransformVisitor::PreVisit(TransformNode* node)
{
    this->GroupVisitor::PreVisit(node);

    m_TransformStack.push(GetTransform().Mul(node->Transform));
}

void TransformVisitor::Visit(TransformNode* node)
{
    this->GroupVisitor::Visit(node);
}

void TransformVisitor::PostVisit(TransformNode* node)
{
    m_TransformStack.pop();

    this->GroupVisitor::PostVisit(node);
}

ModelVisitor::ModelVisitor(RenderGraph* renderGraph)
    : m_RenderGraph(renderGraph)
{
}

void ModelVisitor::Visit(ModelNode* node)
{
    this->TransformVisitor::Visit(node);

    m_RenderGraph->Add(GetTransform(), node);
}

void CameraVisitor::Visit(CameraNode* node)
{
    this->TransformVisitor::Visit(node);

    m_CameraList.emplace_back(ViewspaceCamera
        {
            .Transform = GetTransform(),
            .Projection = node->GetProjection()
        });
}   