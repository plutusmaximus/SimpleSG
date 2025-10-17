#pragma once

#include "ModelNode.h"

class Camera;

class RenderGraph
{
public:

    virtual ~RenderGraph() = 0
    {
    }

    virtual void Add(const Mat44f& transform, RefPtr<Model> model) = 0;

    virtual std::expected<void, Error> Render(const Camera& camera) = 0;

    virtual void Reset() = 0;
};