#include "Camera.h"

void
Camera::SetPerspective(const Degreesf fov, const float width, const float height, const float nearClip, const float farClip)
{
    m_Fov = fov;
    m_Near = nearClip;
    m_Far = farClip;
    SetBounds(width, height);
}

void
Camera::SetBounds(const float width, const float height)
{
    if (width != m_Width || height != m_Height)
    {
        m_Width = width;
        m_Height = height;
        m_Proj = Mat44f::PerspectiveLH(m_Fov, m_Width, m_Height, m_Near, m_Far);
    }
}

const Mat44f&
Camera::GetProjection() const
{
    return m_Proj;
}