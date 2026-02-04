#pragma once

#include "Result.h"
#include "VecMath.h"

class Model;

class RenderGraph
{
public:

    RenderGraph() = default;
    RenderGraph(const RenderGraph&) = delete;
    RenderGraph& operator=(const RenderGraph&) = delete;

    virtual ~RenderGraph() = 0
    {
    }

    virtual void Add(const Mat44f& viewTransform, const Model& model) = 0;

    virtual Result<void> Render(const Mat44f& camera, const Mat44f& projection) = 0;
};