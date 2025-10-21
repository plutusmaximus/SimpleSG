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

    virtual void Add(const Mat44f& transform, RefPtr<Model> model) = 0;

    virtual std::expected<void, Error> Render(const Camera& camera) = 0;

    virtual void Reset() = 0;

    IMPLEMENT_REFCOUNT(RenderGraph);
};