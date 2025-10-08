#include "ModelVisitor.h"

#include "ModelNode.h"
#include "TransformNode.h"
#include "RenderGraph.h"

void ModelVisitor::Visit(ModelNode* node)
{
    m_RenderGraph->Add(GetTransform(), node->Model);
}

void
ModelVisitor::Visit(GroupNode* node)
{
    node->Traverse(this);
}

void
ModelVisitor::Visit(TransformNode* node)
{
    m_TransformStack.push(node->Transform.Mul(GetTransform()));

    Visit((static_cast<GroupNode*>(node)));

    m_TransformStack.pop();
}