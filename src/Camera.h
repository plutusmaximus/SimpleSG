#pragma once

#include "VecMath.h"

class Camera
{
public:

    Camera() = delete;

    Camera(const Degreesf fov, const float width, const float height, const float nearClip, const float farClip)
        : m_Fov(fov)
        , m_Width(0)
        , m_Height(0)
        , m_Near(nearClip)
        , m_Far(farClip)
    {
        m_Transform = Mat44f::Identity();
        SetBounds(width, height);
    }

    void SetBounds(const float width, const float height)
    {
        if (width != m_Width || height != m_Height)
        {
            m_Width = width;
            m_Height = height;
            m_Proj = Mat44f::PerspectiveLH(m_Fov, m_Width, m_Height, m_Near, m_Far);
            m_ViewProj = m_Proj.Mul(m_Transform.Inverse());
        }
    }

    const Mat44f& ViewProj() const
    {
        return m_ViewProj;
    }

private:

    Degreesf m_Fov;
    float m_Width, m_Height;
    float m_Near;
    float m_Far;
    Mat44f m_Transform;
    Mat44f m_Proj;
    Mat44f m_ViewProj;
};