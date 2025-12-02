#pragma once

#include "VecMath.h"

class Camera
{
public:

    Camera() = default;

    void SetPerspective(const Degreesf fov, const float width, const float height, const float nearClip, const float farClip);

    void SetBounds(const float width, const float height);

    const Mat44f& GetProjection() const;

private:

    Degreesf m_Fov{ 0 };
    float m_Width{ 0 }, m_Height{ 0 };
    float m_Near{ 0 };
    float m_Far{ 0 };
    Mat44f m_Proj{ 1 };
};