#include "Projection.h"

void
Projection::SetPerspective(const Radiansf fov, const float aspectRatio, const float nearClip, const float farClip)
{
    m_Fov = fov;
    m_AspectRatio = aspectRatio;
    m_Near = nearClip;
    m_Far = farClip;
    m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_Near, m_Far);
}

const Mat44f&
Projection::GetMatrix() const
{
    return m_Proj;
}