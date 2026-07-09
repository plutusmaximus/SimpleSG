#include "Camera.h"

#include "BoundingVolumes.h"
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

Frustum::Frustum(const Camera& camera, const TrTransformf& cameraXForm) // NOLINT(cppcoreguidelines-pro-type-member-init)
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

Frustum::Frustum(const Camera& camera, const TrTransformf& cameraXForm, const Rect& selectRect) // NOLINT(cppcoreguidelines-pro-type-member-init)
{
    // Screen space points.
    const Vec2f p00(static_cast<float>(selectRect.GetX()), static_cast<float>(selectRect.GetY()));
    const Vec2f p11 = p00 + Vec2f(static_cast<float>(selectRect.GetWidth()),
                                static_cast<float>(selectRect.GetHeight()));
    const Vec2f p01(p00.x, p11.y);
    const Vec2f p10(p11.x, p00.y);

    // Normalized device coordinates.
    const Vec2f ndc00 = ScreenToNdc(p00, camera.GetViewport());
    const Vec2f ndc11 = ScreenToNdc(p11, camera.GetViewport());
    const Vec2f ndc01 = ScreenToNdc(p01, camera.GetViewport());
    const Vec2f ndc10 = ScreenToNdc(p10, camera.GetViewport());

    // Unprojected rays in camera space.
    const float tanHalfFov = std::tan(camera.GetFov().GetValue() * 0.5f);
    const float aspect = camera.GetAspectRatio();
    const float xScale = aspect * tanHalfFov;
    const Vec3f ray00 = Vec3f(ndc00.x * xScale, ndc00.y * tanHalfFov, 1);
    const Vec3f ray11 = Vec3f(ndc11.x * xScale, ndc11.y * tanHalfFov, 1);
    const Vec3f ray01 = Vec3f(ndc01.x * xScale, ndc01.y * tanHalfFov, 1);
    const Vec3f ray10 = Vec3f(ndc10.x * xScale, ndc10.y * tanHalfFov, 1);

    // Frustum plane normals in world space.
    const Vec3f leftNormal = (cameraXForm.R * ray00.Cross(ray01)).Normalize();
    const Vec3f rightNormal = (cameraXForm.R * ray11.Cross(ray10)).Normalize();
    const Vec3f topNormal = (cameraXForm.R * ray10.Cross(ray00)).Normalize();
    const Vec3f bottomNormal = (cameraXForm.R * ray01.Cross(ray11)).Normalize();
    const Vec3f nearNormal = (cameraXForm.R * Vec3f(0, 0, 1)).Normalize();
    const Vec3f farNormal = -nearNormal;

    // Point on the far plane in world space.
    const Vec3f pfar = cameraXForm.T + (cameraXForm.R * Vec3f(0, 0, camera.GetFarClip()));

    // Frustum planes in world space.
    m_Planes[kLeft] = Vec4f(leftNormal, -leftNormal.Dot(cameraXForm.T));
    m_Planes[kRight] = Vec4f(rightNormal, -rightNormal.Dot(cameraXForm.T));
    m_Planes[kTop] = Vec4f(topNormal, -topNormal.Dot(cameraXForm.T));
    m_Planes[kBottom] = Vec4f(bottomNormal, -bottomNormal.Dot(cameraXForm.T));
    m_Planes[kNear] = Vec4f(nearNormal, -nearNormal.Dot(cameraXForm.T));
    m_Planes[kFar] = Vec4f(farNormal, -farNormal.Dot(pfar));
}

Frustum::ContainsResult
Frustum::Contains(const BoundingSphere& sphere) const
{
    const Vec4f pos4(sphere.GetCenter(), 1);
    const float radius = sphere.GetRadius();

    ContainsResult result = ContainsResult::Inside;

    for(const Vec4f& plane : m_Planes)
    {
        const float distance = plane.Dot(pos4);

        if(distance <= -radius)
        {
            return Frustum::ContainsResult::Outside;
        }

        if(distance < radius)
        {
            result = Frustum::ContainsResult::Intersects;
        }
    }

    return result;
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
    SetAspectRatio(viewport.GetAspectRatio());
}

const Mat44f&
Camera::GetMatrix() const
{
    return m_Proj;
}