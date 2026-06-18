#include "Camera.h"

#include "Bounds.h"

Viewport::Viewport(const ViewportParams& params)
    : m_X(params.x),
      m_Y(params.y),
      m_Width(params.width),
      m_Height(params.height),
      m_MinDepth(params.minDepth),
      m_MaxDepth(params.maxDepth)
{
    MLG_ASSERT(params.maxDepth >= 0.0f && params.maxDepth <= 1.0f, "Max depth must be in the range [0, 1]");
    MLG_ASSERT(params.maxDepth > params.minDepth, "Max depth must be greater than min depth");
    MLG_ASSERT(params.width > 0, "Viewport width must be greater than 0");
    MLG_ASSERT(params.height > 0, "Viewport height must be greater than 0");
}

Frustum::Frustum(const Camera& camera, const Posef& cameraXForm) // NOLINT(cppcoreguidelines-pro-type-member-init)
{
    const Mat44f VP = camera.GetMatrix() * cameraXForm.Inverse().ToMatrix();
    const Vec4f r0(VP[0][0], VP[1][0], VP[2][0], VP[3][0]);
    const Vec4f r1(VP[0][1], VP[1][1], VP[2][1], VP[3][1]);
    const Vec4f r2(VP[0][2], VP[1][2], VP[2][2], VP[3][2]);
    const Vec4f r3(VP[0][3], VP[1][3], VP[2][3], VP[3][3]);

    m_Planes[kLeft] = Vec4f(r3 + r0);
    m_Planes[kRight] = Vec4f(r3 - r0);
    m_Planes[kTop] = Vec4f(r3 - r1);
    m_Planes[kBottom] = Vec4f(r3 + r1);
    m_Planes[kNear] = Vec4f(r2);
    m_Planes[kFar] = Vec4f(r3 - r2);

    const float ll = Vec3f(m_Planes[kLeft].x, m_Planes[kLeft].y, m_Planes[kLeft].z).Length();
    const float lr = Vec3f(m_Planes[kRight].x, m_Planes[kRight].y, m_Planes[kRight].z).Length();
    const float lt = Vec3f(m_Planes[kTop].x, m_Planes[kTop].y, m_Planes[kTop].z).Length();
    const float lb = Vec3f(m_Planes[kBottom].x, m_Planes[kBottom].y, m_Planes[kBottom].z).Length();
    const float ln = Vec3f(m_Planes[kNear].x, m_Planes[kNear].y, m_Planes[kNear].z).Length();
    const float lf = Vec3f(m_Planes[kFar].x, m_Planes[kFar].y, m_Planes[kFar].z).Length();
    m_Planes[kLeft] /= ll;
    m_Planes[kRight] /= lr;
    m_Planes[kTop] /= lt;
    m_Planes[kBottom] /= lb;
    m_Planes[kNear] /= ln;
    m_Planes[kFar] /= lf;
}

bool
Frustum::Contains(const BoundingSphere& sphere, const Vec3f& pos) const
{
    const Vec4f pos4(pos + sphere.GetCenter(), 1);
    const float radius = sphere.GetRadius();

    for(const Vec4f& plane : m_Planes)
    {
        if(plane.Dot(pos4) <= -radius)
        {
            return false;
        }
    }

    return true;
}

void
Camera::SetPerspective(const Radiansf fov,
    const float aspectRatio,
    const float nearClip,
    const float farClip,
    const Viewport& viewport)
{
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
    m_Viewport = viewport;
}

const Mat44f&
Camera::GetMatrix() const
{
    return m_Proj;
}