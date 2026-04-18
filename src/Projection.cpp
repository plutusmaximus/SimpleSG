#include "Projection.h"

void
Projection::SetPerspective(const Radiansf fov, const Extent& screenBounds, const float nearClip, const float farClip)
{
    m_Fov = fov;
    m_Near = nearClip;
    m_Far = farClip;
    SetBounds(screenBounds);
}

void
Projection::SetBounds(const Extent& screenBounds)
{
    if (screenBounds != m_Bounds)
    {
        m_Bounds = screenBounds;
        m_Proj = Mat44f::PerspectiveLH(m_Fov, m_Bounds.Width, m_Bounds.Height, m_Near, m_Far);
    }
}

const Mat44f&
Projection::GetMatrix() const
{
    return m_Proj;
}