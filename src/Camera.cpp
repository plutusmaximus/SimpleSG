#include "Camera.h"

Viewport::Viewport(const uint32_t x,
    const uint32_t y,
    const uint32_t width,
    const uint32_t height,
    const float minDepth,
    const float maxDepth)
    : m_X(x),
      m_Y(y),
      m_Width(width),
      m_Height(height),
      m_MinDepth(minDepth),
      m_MaxDepth(maxDepth)
{
    MLG_ASSERT(maxDepth >= 0.0f && maxDepth <= 1.0f, "Max depth must be in the range [0, 1]");
    MLG_ASSERT(maxDepth > minDepth, "Max depth must be greater than min depth");
    MLG_ASSERT(width > 0, "Viewport width must be greater than 0");
    MLG_ASSERT(height > 0, "Viewport height must be greater than 0");
}

void
Camera::SetPerspective(const Radiansf fov,
    const float aspectRatio,
    const float nearClip,
    const float farClip,
    const Viewport& viewport)
{
    MLG_ASSERT(viewport.IsValid(), "Viewport must be valid");
    MLG_ASSERT(fov.GetValue() > 0 && fov.GetValue() < std::numbers::pi_v<float>,
        "FOV must be between 0 and 180 degrees");
    MLG_ASSERT(nearClip > 0, "Near clip must be greater than 0");
    MLG_ASSERT(farClip > 0, "Far clip must be greater than 0");
    MLG_ASSERT(farClip > nearClip, "Far clip must be greater than near clip");
    MLG_ASSERT(aspectRatio > 0, "Aspect ratio must be greater than 0");

    m_Fov = fov;
    m_AspectRatio = aspectRatio;
    m_NearClip = nearClip;
    m_FarClip = farClip;
    m_Viewport = viewport;
    m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_NearClip, m_FarClip);
}

void
Camera::SetFov(const Radiansf fov)
{
    MLG_ASSERT(fov.GetValue() > 0 && fov.GetValue() < std::numbers::pi_v<float>,
        "FOV must be between 0 and 180 degrees");

    m_Fov = fov;
    m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_NearClip, m_FarClip);
}

void
Camera::SetNearClip(const float nearClip)
{
    MLG_ASSERT(nearClip > 0, "Near clip must be greater than 0");
    MLG_ASSERT(nearClip < m_FarClip, "Near clip must be less than far clip");

    m_NearClip = nearClip;
    m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_NearClip, m_FarClip);
}

void
Camera::SetFarClip(const float farClip)
{
    MLG_ASSERT(farClip > 0, "Far clip must be greater than 0");
    MLG_ASSERT(farClip > m_NearClip, "Far clip must be greater than near clip");

    m_FarClip = farClip;
    m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_NearClip, m_FarClip);
}

void
Camera::SetAspectRatio(const float aspectRatio)
{
    MLG_ASSERT(aspectRatio > 0, "Aspect ratio must be greater than 0");

    m_AspectRatio = aspectRatio;
    m_Proj = Mat44f::PerspectiveLH(m_Fov, m_AspectRatio, m_NearClip, m_FarClip);
}

void
Camera::SetViewport(const Viewport& viewport)
{
    MLG_ASSERT(viewport.IsValid(), "Viewport must be valid");

    m_Viewport = viewport;
}

const Mat44f&
Camera::GetMatrix() const
{
    return m_Proj;
}