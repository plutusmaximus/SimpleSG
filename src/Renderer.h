#pragma once

#include "Result.h"

class Model;
template<typename T>
class Mat44;
using Mat44f = Mat44<float>;

class Renderer
{
public:

    Renderer() = default;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    virtual ~Renderer() = 0
    {
    }

    virtual void Add(const Mat44f& viewTransform, const Model* model) = 0;

    virtual Result<void> Render(const Mat44f& camera, const Mat44f& projection) = 0;
};