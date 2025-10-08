#pragma once

class ModelNode;
class GroupNode;
class TransformNode;

class SceneVisitor
{
public:

    SceneVisitor() = default;

    virtual ~SceneVisitor() = 0 {}

    virtual void Visit(ModelNode* node) = 0;

    virtual void Visit(TransformNode* node) = 0;

    virtual void Visit(GroupNode* node) = 0;
};