#pragma once

#include "GroupNode.h"
#include "VecMath.h"

class SceneVisitor;

class TransformNode : public GroupNode
{
public:

    TransformNode()
    {
        Transform = Mat44f::Identity();
    }

    ~TransformNode() override {}

    void Accept(SceneVisitor* visitor) override
    {
        visitor->Visit(this);
    }

    Mat44f Transform;
};