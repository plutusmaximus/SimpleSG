#pragma once

#include "RefCount.h"
#include "VecMath.h"
#include "Error.h"

class Model;

class RenderGraph
{
public:

    RenderGraph() = default;

    virtual ~RenderGraph() = 0
    {
    }

    virtual void Add(const Mat44f& viewTransform, RefPtr<Model> model) = 0;

    virtual Result<void> Render(const Mat44f& camera, const Mat44f& projection) = 0;
};