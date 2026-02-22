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

    /// @brief Called once per frame before any calls to AddModel() or Render().
    virtual Result<void> NewFrame() = 0;

    /// @brief Adds a model to be rendered for the current frame. The viewTransform is the
    virtual void AddModel(const Mat44f& viewTransform, const Model* model) = 0;

    /// @brief Renders the current frame using the provided camera and projection matrices.
    virtual Result<void> Render(const Mat44f& camera, const Mat44f& projection) = 0;
};