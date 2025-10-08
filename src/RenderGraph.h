#pragma once

#include "Model.h"

class Camera;
class Model;

class RenderGraph
{
public:

    virtual ~RenderGraph() = 0
    {
    }

    virtual void Add(const Mat44f& transform, RefPtr<Model> model) = 0;

    virtual void Render(const Camera& camera) = 0;

    virtual void Reset() = 0;
};