#pragma once

#include "VecMath.h"

class Projection
{
public:

    Projection() = default;

    void SetPerspective(
        const Radiansf fov, const float aspectRatio, const float nearClip, const float farClip);

    void SetFov(const Radiansf fov)
    {
        MLG_ASSERT(fov.Value() > 0 && fov.Value() < std::numbers::pi_v<float>, "FOV must be between 0 and 180 degrees");

        m_Fov = fov;
        m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_Near, m_Far);
    }
    Radiansf GetFov() const { return m_Fov; }

    void SetNearClip(const float nearClip)
    {
        MLG_ASSERT(nearClip > 0, "Near clip must be greater than 0");
        MLG_ASSERT(nearClip < m_Far, "Near clip must be less than far clip");

        m_Near = nearClip;
        m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_Near, m_Far);
    }

    float GetNearClip() const { return m_Near; }

    void SetFarClip(const float farClip)
    {
        MLG_ASSERT(farClip > 0, "Far clip must be greater than 0");
        MLG_ASSERT(farClip > m_Near, "Far clip must be greater than near clip");

        m_Far = farClip;
        m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_Near, m_Far);
    }

    float GetFarClip() const { return m_Far; }

    void SetAspectRatio(const float aspectRatio)
    {
        MLG_ASSERT(aspectRatio > 0, "Aspect ratio must be greater than 0");

        m_AspectRatio = aspectRatio;
        m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_Near, m_Far);
    }

    float GetAspectRatio() const { return m_AspectRatio; }

    const Mat44f& GetMatrix() const;

private:

    Radiansf m_Fov{Radiansf::FromDegrees(45)};
    float m_AspectRatio{1};
    float m_Near{ 0.1f };
    float m_Far{ 1000.0f };
    Mat44f m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_Near, m_Far);
};