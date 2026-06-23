#include "Camera.h"

#include "Bounds.h"
#include "VecMath.h"

namespace
{
Vec2f ScreenToNdc(const Vec2f& screenPos, const Viewport& viewport)
{
    const float x = (screenPos.x - static_cast<float>(viewport.GetX())) / static_cast<float>(viewport.GetWidth());
    const float y = (screenPos.y - static_cast<float>(viewport.GetY())) / static_cast<float>(viewport.GetHeight());

    return Vec2f((2.0f * x) - 1.0f, 1.0f - (2.0f * y));
}
} // namespace

Viewport::Viewport(const ViewportParams& params)
    : m_X(params.x),
      m_Y(params.y),
      m_Width(params.width),
      m_Height(params.height),
      m_MinDepth(params.minDepth),
      m_MaxDepth(params.maxDepth)
{
    MLG_ABORTIF(params.maxDepth < 0.0f || params.maxDepth > 1.0f, "Max depth must be in the range [0, 1]");
    MLG_ABORTIF(params.maxDepth <= params.minDepth, "Max depth must be greater than min depth");
    MLG_ABORTIF(params.width <= 0, "Viewport width must be greater than 0");
    MLG_ABORTIF(params.height <= 0, "Viewport height must be greater than 0");
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

Frustum::Frustum(const Camera& camera, const Posef& cameraXForm, const Rect& selectRect) // NOLINT(cppcoreguidelines-pro-type-member-init)
{
    Vec2f ndc00(static_cast<float>(selectRect.GetX()), static_cast<float>(selectRect.GetY()));
    Vec2f ndc11 = ndc00 + Vec2f(static_cast<float>(selectRect.GetWidth()), static_cast<float>(selectRect.GetHeight()));
    Vec2f ndc01(ndc00.x, ndc11.y);
    Vec2f ndc10(ndc11.x, ndc00.y);
    
    ndc00 = ScreenToNdc(ndc00, camera.GetViewport());
    ndc11 = ScreenToNdc(ndc11, camera.GetViewport());
    ndc01 = ScreenToNdc(ndc01, camera.GetViewport());
    ndc10 = ScreenToNdc(ndc10, camera.GetViewport());

    // Compute the inverse of the view-projection matrix (invVP) to transform from NDC to world space
    // VP = P * view and view = cameraWorld^-1
    // Therefore, invVP = (P * cameraWorld^-1)^-1 = cameraWorld * P^-1

    const Mat44f invVP = cameraXForm.ToMatrix() * camera.GetMatrix().Inverse();

    const Vec4f near00 = invVP * Vec4f(ndc00, 0, 1.0f);
    const Vec4f near11 = invVP * Vec4f(ndc11, 0, 1.0f);
    const Vec4f near01 = invVP * Vec4f(ndc01, 0, 1.0f);
    const Vec4f near10 = invVP * Vec4f(ndc10, 0, 1.0f);

    const Vec4f far00 = invVP * Vec4f(ndc00, 1, 1.0f);
    //const Vec4f far11 = invVP * Vec4f(ndc11, 1, 1.0f);
    const Vec4f far01 = invVP * Vec4f(ndc01, 1, 1.0f);
    const Vec4f far10 = invVP * Vec4f(ndc10, 1, 1.0f);

    const Vec3f worldNear00 = near00.xyz() / near00.w;
    const Vec3f worldNear11 = near11.xyz() / near11.w;
    const Vec3f worldNear01 = near01.xyz() / near01.w;
    const Vec3f worldNear10 = near10.xyz() / near10.w;
    const Vec3f worldFar00 = far00.xyz() / far00.w;
    //const Vec3f worldFar11 = far11.xyz() / far11.w;
    const Vec3f worldFar01 = far01.xyz() / far01.w;
    const Vec3f worldFar10 = far10.xyz() / far10.w;

    const Vec3f leftNormal = (worldFar00 - worldNear00).Cross(worldNear01 - worldNear00).Normalize();
    const Vec3f rightNormal = (worldNear11 - worldNear10).Cross(worldFar10 - worldNear10).Normalize();
    const Vec3f topNormal = (worldNear10 - worldNear00).Cross(worldFar00 - worldNear00).Normalize();
    const Vec3f bottomNormal = (worldFar01 - worldNear01).Cross(worldNear11 - worldNear01).Normalize();
    const Vec3f nearNormal = (worldNear01 - worldNear00).Cross(worldNear10 - worldNear00).Normalize();
    const Vec3f farNormal = (worldFar10 - worldFar00).Cross(worldFar01 - worldFar00).Normalize();

    const float leftD = -leftNormal.Dot(worldNear00);
    const float rightD = -rightNormal.Dot(worldNear10);
    const float topD = -topNormal.Dot(worldNear00);
    const float bottomD = -bottomNormal.Dot(worldNear01);
    const float nearD = -nearNormal.Dot(worldNear00);
    const float farD = -farNormal.Dot(worldFar00);

    m_Planes[kLeft] = Vec4f(leftNormal, leftD);
    m_Planes[kRight] = Vec4f(rightNormal, rightD);
    m_Planes[kTop] = Vec4f(topNormal, topD);
    m_Planes[kBottom] = Vec4f(bottomNormal, bottomD);
    m_Planes[kNear] = Vec4f(nearNormal, nearD);
    m_Planes[kFar] = Vec4f(farNormal, farD);
}

bool
Frustum::Contains(const BoundingSphere& sphere) const
{
    const Vec4f pos4(sphere.GetCenter(), 1);
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