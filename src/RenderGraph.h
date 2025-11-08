#pragma once

#include "RefCount.h"
#include "VecMath.h"
#include "Error.h"

class ModelNode;

class RenderGraph
{
public:

    RenderGraph() = default;

    virtual ~RenderGraph() = 0
    {
    }

    virtual void Add(const Mat44f& viewTransform, RefPtr<ModelNode> model) = 0;

    virtual Result<void> Render(const Mat44f& camera, const Mat44f& projection) = 0;

    virtual void Reset() = 0;
};