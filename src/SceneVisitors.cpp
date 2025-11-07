#include "SceneVisitors.h"

#include "SceneNodes.h"
#include "RenderGraph.h"

void GroupVisitor::Visit(GroupNode* node)
{
    node->Traverse(this);
}

void TransformVisitor::Visit(TransformNode* node)
{
    m_TransformStack.push(m_TransformStack.top().Mul(node->Transform));

    this->GroupVisitor::Visit(node);

    m_TransformStack.pop();
}

ModelVisitor::ModelVisitor(RefPtr<RenderGraph> renderGraph)
    : m_RenderGraph(renderGraph)
{
}

void ModelVisitor::Visit(ModelNode* node)
{
    m_RenderGraph->Add(GetTransform(), node);
}

void CameraVisitor::Visit(CameraNode* node)
{
    m_CameraList.emplace_back(ViewspaceCamera
        {
            .Transform = GetTransform(),
            .Projection = node->GetProjection()
        });
}   