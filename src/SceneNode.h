#pragma once

#include <vector>

#include "RefCount.h"

class SceneVisitor;

class SceneNode
{
public:

    SceneNode() = default;

    virtual ~SceneNode() = 0 {}

    virtual void Accept(SceneVisitor* visitor) = 0;

private:

    IMPLEMENT_REFCOUNT(SceneNode);
};