#pragma once

#include "VecMath.h"

class Camera
{
public:

    Camera() = default;

    void SetPerspective(const Radiansf fov, const Extent& screenBounds, const float nearClip, const float farClip);

    void SetBounds(const Extent& screenBounds);

    const Mat44f& GetProjection() const;

private:

    Radiansf m_Fov;
    Extent m_Bounds{ 0,0 };
    float m_Near{ 0 };
    float m_Far{ 0 };
    Mat44f m_Proj{ 1 };
};