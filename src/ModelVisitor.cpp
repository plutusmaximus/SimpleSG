#include "ModelVisitor.h"

#include "Model.h"
#include "TransformNode.h"
#include "RenderGraph.h"

void ModelVisitor::Visit(Model* node)
{
    m_RenderGraph->Add(m_TransformStack.top(), node);
}

void
ModelVisitor::Visit(GroupNode* node)
{
    node->Traverse(this);
}

void
ModelVisitor::Visit(TransformNode* node)
{
    m_TransformStack.push(m_TransformStack.top().Mul(node->Transform));

    Visit((static_cast<GroupNode*>(node)));

    m_TransformStack.pop();
}