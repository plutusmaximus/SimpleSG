#pragma once

#include "Model.h"
#include "Error.h"

#include <expected>

class Camera;

class RenderGraph
{
public:

    RenderGraph() = default;

    virtual ~RenderGraph() = 0
    {
    }

    virtual void Add(const Mat44f& viewTransform, RefPtr<Model> model) = 0;

    virtual Result<void> Render(const Mat44f& view, const Mat44f& projection) = 0;

    virtual void Reset() = 0;

    IMPLEMENT_REFCOUNT(RenderGraph);
};