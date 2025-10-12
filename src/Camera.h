#pragma once

#include "VecMath.h"

class Camera
{
public:

    Camera() = delete;

    Camera(const Degreesf fov, const float aspect, const float nearClip, const float farClip)
        : m_Fov(fov)
        , m_Aspect(aspect)
        , m_Near(nearClip)
        , m_Far(farClip)
        , m_ComputeViewProj(true)
    {
        m_ViewProj = m_Transform = Mat44f::Identity();
    }

    void SetAspect(const float aspect)
    {
        m_Aspect = aspect;
        m_ComputeViewProj = true;
    }

    const Mat44f& ViewProj() const
    {
        if (m_ComputeViewProj)
        {
            Mat44f proj = Mat44f::PerspectiveLH(m_Fov, m_Aspect, m_Near, m_Far);
            m_ViewProj = m_Transform.Inverse().Mul(proj);
            m_ComputeViewProj = false;
        }

        return m_ViewProj;
    }

private:

    Degreesf m_Fov;
    float m_Aspect;
    float m_Near;
    float m_Far;
    Mat44f m_Transform;
    mutable Mat44f m_ViewProj;

    mutable bool m_ComputeViewProj;
};