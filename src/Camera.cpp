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


Frustum::Frustum(const Camera& camera, const Posef& pose) // NOLINT(cppcoreguidelines-pro-type-member-init)
{
    const Mat44f VP = camera.GetMatrix() * pose.Inverse().ToMatrix();
    const Vec4f r0(VP[0][0], VP[1][0], VP[2][0], VP[3][0]);
    const Vec4f r1(VP[0][1], VP[1][1], VP[2][1], VP[3][1]);
    const Vec4f r2(VP[0][2], VP[1][2], VP[2][2], VP[3][2]);
    const Vec4f r3(VP[0][3], VP[1][3], VP[2][3], VP[3][3]);

    m_Left = Vec4f(r3 + r0);
    m_Right = Vec4f(r3 - r0);
    m_Top = Vec4f(r3 - r1);
    m_Bottom = Vec4f(r3 + r1);
    m_Near = Vec4f(r2);
    m_Far = Vec4f(r3 - r2);

    const float ll = Vec3f(m_Left.x, m_Left.y, m_Left.z).Length();
    const float lr = Vec3f(m_Right.x, m_Right.y, m_Right.z).Length();
    const float lt = Vec3f(m_Top.x, m_Top.y, m_Top.z).Length();
    const float lb = Vec3f(m_Bottom.x, m_Bottom.y, m_Bottom.z).Length();
    const float ln = Vec3f(m_Near.x, m_Near.y, m_Near.z).Length();
    const float lf = Vec3f(m_Far.x, m_Far.y, m_Far.z).Length();
    m_Left /= ll;
    m_Right /= lr;
    m_Top /= lt;
    m_Bottom /= lb;
    m_Near /= ln;
    m_Far /= lf;
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