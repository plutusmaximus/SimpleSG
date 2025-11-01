#pragma once

class ModelNode;
class CameraNode;
class GroupNode;
class TransformNode;

class SceneVisitor
{
public:

    SceneVisitor() = default;

    virtual ~SceneVisitor() = 0 {}

    virtual void Visit(ModelNode* node) {}

    virtual void Visit(CameraNode* node) {}

    virtual void Visit(TransformNode* node) {}

    virtual void Visit(GroupNode* node) {}
};