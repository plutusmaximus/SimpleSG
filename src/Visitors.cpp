#include "Visitors.h"

#include "Model.h"
#include "TransformNode.h"
#include "Camera.h"
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

void ModelVisitor::Visit(Model* node)
{
    m_RenderGraph->Add(GetTransform(), node);
}

void CameraVisitor::Visit(Camera* node)
{
    m_CameraList.emplace_back(ViewspaceCamera{ .ViewTransform = GetTransform(), .Camera = node });
}