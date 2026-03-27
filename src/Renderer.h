#pragma once

#include "Result.h"

class Model;
template<typename T>
class Mat44;
using Mat44f = Mat44<float>;
class RenderCompositor;

class ScenePack;

class Renderer
{
public:

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    Renderer(Renderer&&) = delete;
    Renderer& operator=(Renderer&&) = delete;

    virtual ~Renderer() = 0
    {
    }

    /// @brief Renders the current frame using the provided camera and projection matrices.
    virtual Result<> Render(const Mat44f& camera,
        const Mat44f& projection,
        const Model* model,
        RenderCompositor* compositor) = 0;

    virtual Result<> Render(const Mat44f& camera,
        const Mat44f& projection,
        const ScenePack& scenePack,
        RenderCompositor* compositor) = 0;

protected:
    Renderer() = default;
};