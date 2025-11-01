#pragma once

class Model;
class Camera;
class GroupNode;
class TransformNode;

class SceneVisitor
{
public:

    SceneVisitor() = default;

    virtual ~SceneVisitor() = 0 {}

    virtual void Visit(Model* node) {}

    virtual void Visit(Camera* node) {}

    virtual void Visit(TransformNode* node) {}

    virtual void Visit(GroupNode* node) {}
};