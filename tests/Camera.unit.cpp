#include "Camera.h"

#include "BoundingVolumes.h"

#include <gtest/gtest.h>

// NOLINTBEGIN(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)

namespace
{
constexpr float EPS = 1e-5f;
using ContainsResult = Frustum::ContainsResult;

Viewport
MakeViewport(const uint32_t x = 0,
    const uint32_t y = 0,
    const uint32_t width = 1280,
    const uint32_t height = 720,
    const float minDepth = 0.0f,
    const float maxDepth = 1.0f)
{
    return Viewport({ .x = x,
        .y = y,
        .width = width,
        .height = height,
        .minDepth = minDepth,
        .maxDepth = maxDepth });
}

void
ExpectMatrixNear(const Mat44f& actual, const Mat44f& expected)
{
    for(size_t col = 0; col < 4; ++col)
    {
        for(size_t row = 0; row < 4; ++row)
        {
            EXPECT_NEAR(actual[col][row], expected[col][row], EPS);
        }
    }
}

void
ExpectViewportEq(const Viewport& viewport,
    const uint32_t x,
    const uint32_t y,
    const uint32_t width,
    const uint32_t height,
    const float minDepth,
    const float maxDepth)
{
    EXPECT_EQ(viewport.GetX(), x);
    EXPECT_EQ(viewport.GetY(), y);
    EXPECT_EQ(viewport.GetWidth(), width);
    EXPECT_EQ(viewport.GetHeight(), height);
    EXPECT_FLOAT_EQ(viewport.GetMinDepth(), minDepth);
    EXPECT_FLOAT_EQ(viewport.GetMaxDepth(), maxDepth);
}

float
SignedDistanceToPlane(const Vec4f& plane, const Vec3f& point)
{
    return plane.Dot(Vec4f(point, 1.0f));
}

Vec3f
PlaneNormal(const Vec4f& plane)
{
    return Vec3f(plane.x, plane.y, plane.z);
}

Vec3f
MoveToSignedDistanceFromPlane(const Vec4f& plane, const Vec3f& point, const float signedDistance)
{
    const float pointDistance = SignedDistanceToPlane(plane, point);
    return point + (PlaneNormal(plane) * (signedDistance - pointDistance));
}

Camera
MakeFrustumCamera()
{
    Camera camera(MakeViewport(11, 17, 137, 89));
    camera.SetPerspective(Radiansf::FromDegrees(73.0f),
        137.0f / 89.0f,
        0.71f,
        18.3f,
        camera.GetViewport());
    return camera;
}

TrTransformf
MakeFrustumCameraXform()
{
    TrTransformf xform;
    xform.T = Vec3f(4.7f, -2.3f, 8.1f);
    xform.R = UnitQuatf(Radiansf::FromDegrees(31.0f), Vec3f::YAXIS())
        * UnitQuatf(Radiansf::FromDegrees(-17.0f), Vec3f::XAXIS());
    return xform;
}

Vec3f
WorldFromCameraPoint(const TrTransformf& cameraXform, const Vec3f& cameraPoint)
{
    return cameraXform * cameraPoint;
}

Vec2f
ScreenToNdcForTest(const Vec2f& screenPos, const Viewport& viewport)
{
    const float x = (screenPos.x - static_cast<float>(viewport.GetX()))
        / static_cast<float>(viewport.GetWidth());
    const float y = (screenPos.y - static_cast<float>(viewport.GetY()))
        / static_cast<float>(viewport.GetHeight());
    return Vec2f((2.0f * x) - 1.0f, 1.0f - (2.0f * y));
}

Vec3f
WorldFromScreenPoint(const Camera& camera,
    const TrTransformf& cameraXform,
    const Vec2f& screenPos,
    const float ndcDepth)
{
    const Vec2f ndc = ScreenToNdcForTest(screenPos, camera.GetViewport());
    const Mat44f invViewProj = cameraXform.ToMatrix() * camera.GetMatrix().Inverse();
    const Vec4f world = invViewProj * Vec4f(ndc, ndcDepth, 1.0f);
    return world.xyz() / world.w;
}
} // namespace

TEST(Viewport, Constructor_StoresParamsAndComputesAspectRatio)
{
    const Viewport viewport = MakeViewport(17, 23, 1919, 1073, 0.19f, 0.83f);

    ExpectViewportEq(viewport, 17, 23, 1919, 1073, 0.19f, 0.83f);
    EXPECT_FLOAT_EQ(viewport.GetAspectRatio(), 1919.0f / 1073.0f);
}

TEST(Viewport, Constructor_FromExtentUsesFullDepthAndOrigin)
{
    const Viewport viewport(Dimension2{ .Width = 853, .Height = 497 });

    ExpectViewportEq(viewport, 0, 0, 853, 497, 0.0f, 1.0f);
    EXPECT_FLOAT_EQ(viewport.GetAspectRatio(), 853.0f / 497.0f);
}

TEST(Camera, Constructor_UsesViewportAspectRatioWithDefaultProjection)
{
    const Viewport viewport = MakeViewport(0, 0, 853, 497);
    const Camera camera(viewport);

    EXPECT_NEAR(camera.GetFov().GetValue(), Radiansf::FromDegrees(45.0f).GetValue(), EPS);
    EXPECT_FLOAT_EQ(camera.GetAspectRatio(), 853.0f / 497.0f);
    EXPECT_FLOAT_EQ(camera.GetNearClip(), 0.1f);
    EXPECT_FLOAT_EQ(camera.GetFarClip(), 1000.0f);
    ExpectViewportEq(camera.GetViewport(), 0, 0, 853, 497, 0.0f, 1.0f);
    ExpectMatrixNear(camera.GetMatrix(),
        Mat44f::PerspectiveLH(camera.GetFov(),
            camera.GetAspectRatio(),
            camera.GetNearClip(),
            camera.GetFarClip()));
}

TEST(Camera, SetPerspective_UpdatesProjectionParamsViewportAndMatrix)
{
    Camera camera(MakeViewport());
    const Viewport viewport = MakeViewport(7, 13, 1031, 587, 0.13f, 0.91f);
    const Radiansf fov = Radiansf::FromDegrees(57.0f);
    const float aspectRatio = 1031.0f / 587.0f;
    const float nearClip = 0.37f;
    const float farClip = 263.0f;

    camera.SetPerspective(fov, aspectRatio, nearClip, farClip, viewport);

    EXPECT_NEAR(camera.GetFov().GetValue(), fov.GetValue(), EPS);
    EXPECT_FLOAT_EQ(camera.GetAspectRatio(), aspectRatio);
    EXPECT_FLOAT_EQ(camera.GetNearClip(), nearClip);
    EXPECT_FLOAT_EQ(camera.GetFarClip(), farClip);
    ExpectViewportEq(camera.GetViewport(), 7, 13, 1031, 587, 0.13f, 0.91f);
    ExpectMatrixNear(camera.GetMatrix(),
        Mat44f::PerspectiveLH(fov, aspectRatio, nearClip, farClip));
}

TEST(Camera, Setters_UpdateProjectionMatrix)
{
    Camera camera(MakeViewport());

    camera.SetFov(Radiansf::FromDegrees(68.0f));
    camera.SetAspectRatio(1417.0f / 823.0f);
    camera.SetNearClip(0.43f);
    camera.SetFarClip(617.0f);

    EXPECT_NEAR(camera.GetFov().GetValue(), Radiansf::FromDegrees(68.0f).GetValue(), EPS);
    EXPECT_FLOAT_EQ(camera.GetAspectRatio(), 1417.0f / 823.0f);
    EXPECT_FLOAT_EQ(camera.GetNearClip(), 0.43f);
    EXPECT_FLOAT_EQ(camera.GetFarClip(), 617.0f);
    ExpectMatrixNear(camera.GetMatrix(),
        Mat44f::PerspectiveLH(camera.GetFov(),
            camera.GetAspectRatio(),
            camera.GetNearClip(),
            camera.GetFarClip()));
}

TEST(Camera, SetViewport_ChangesProjectionAspectRatio)
{
    Camera camera(MakeViewport(0, 0, 853, 497));
    camera.SetAspectRatio(1361.0f / 769.0f);
    const Viewport viewport = MakeViewport(29, 37, 317, 907, 0.23f, 0.81f);

    camera.SetViewport(viewport);

    ExpectViewportEq(camera.GetViewport(), 29, 37, 317, 907, 0.23f, 0.81f);
    EXPECT_FLOAT_EQ(camera.GetAspectRatio(), 317.0f / 907.0f);
    ExpectMatrixNear(camera.GetMatrix(),
        Mat44f::PerspectiveLH(camera.GetFov(),
            camera.GetAspectRatio(),
            camera.GetNearClip(),
            camera.GetFarClip()));
}

TEST(Frustum, Contains_ReturnsInsideForSphereInsideCameraView)
{
    const Camera camera = MakeFrustumCamera();
    const TrTransformf cameraXform = MakeFrustumCameraXform();
    const Frustum frustum(camera, cameraXform);

    EXPECT_EQ(
        frustum.Contains(
            BoundingSphere(WorldFromCameraPoint(cameraXform, Vec3f(0.31f, -0.27f, 6.4f)), 0.43f)),
        ContainsResult::Inside);
    EXPECT_EQ(
        frustum.Contains(
            BoundingSphere(WorldFromCameraPoint(cameraXform, Vec3f(1.7f, 0.83f, 9.2f)), 0.61f)),
        ContainsResult::Inside);
}

TEST(Frustum, Contains_ReturnsOutsideForSphereOutsideCameraView)
{
    const Camera camera = MakeFrustumCamera();
    const TrTransformf cameraXform = MakeFrustumCameraXform();
    const Frustum frustum(camera, cameraXform);

    EXPECT_EQ(
        frustum.Contains(
            BoundingSphere(WorldFromCameraPoint(cameraXform, Vec3f(0.19f, -0.11f, 19.1f)), 0.31f)),
        ContainsResult::Outside);
    EXPECT_EQ(
        frustum.Contains(
            BoundingSphere(WorldFromCameraPoint(cameraXform, Vec3f(9.7f, 0.41f, 6.2f)), 0.37f)),
        ContainsResult::Outside);
}

TEST(Frustum, Contains_ReturnsIntersectsForSphereStraddlingFrustumPlanes)
{
    const Camera camera = MakeFrustumCamera();
    const TrTransformf cameraXform = MakeFrustumCameraXform();
    const Frustum frustum(camera, cameraXform);

    const Vec3f interiorPoint = WorldFromCameraPoint(cameraXform, Vec3f(0.31f, -0.27f, 6.4f));
    const Vec4f& leftPlane = frustum.GetLeft();
    const float leftInteriorDistance = SignedDistanceToPlane(leftPlane, interiorPoint);
    const Vec3f leftStraddlingCenter =
        interiorPoint - (PlaneNormal(leftPlane) * (leftInteriorDistance + 0.29f));

    EXPECT_LT(SignedDistanceToPlane(leftPlane, leftStraddlingCenter), 0.0f);
    EXPECT_EQ(frustum.Contains(BoundingSphere(leftStraddlingCenter, 0.67f)),
        ContainsResult::Intersects);

    const Vec4f& farPlane = frustum.GetFar();
    const float farInteriorDistance = SignedDistanceToPlane(farPlane, interiorPoint);
    const Vec3f farStraddlingCenter =
        interiorPoint - (PlaneNormal(farPlane) * (farInteriorDistance + 0.37f));
    EXPECT_LT(SignedDistanceToPlane(frustum.GetFar(), farStraddlingCenter), 0.0f);
    EXPECT_EQ(frustum.Contains(BoundingSphere(farStraddlingCenter, 0.73f)),
        ContainsResult::Intersects);
}

TEST(Frustum, Contains_HandlesSpheresTangentToFrustumPlanes)
{
    const Camera camera = MakeFrustumCamera();
    const TrTransformf cameraXform = MakeFrustumCameraXform();
    const Frustum frustum(camera, cameraXform);

    const Vec3f interiorPoint = WorldFromCameraPoint(cameraXform, Vec3f(0.31f, -0.27f, 6.4f));
    const float leftRadius = 0.47f;
    const Vec3f leftInsideTangentCenter =
        MoveToSignedDistanceFromPlane(frustum.GetLeft(), interiorPoint, leftRadius);
    const Vec3f leftOutsideTangentCenter =
        MoveToSignedDistanceFromPlane(frustum.GetLeft(), interiorPoint, -leftRadius);

    EXPECT_NEAR(SignedDistanceToPlane(frustum.GetLeft(), leftInsideTangentCenter), leftRadius, EPS);
    EXPECT_EQ(frustum.Contains(BoundingSphere(leftInsideTangentCenter, leftRadius)),
        ContainsResult::Intersects);
    EXPECT_NEAR(SignedDistanceToPlane(frustum.GetLeft(), leftOutsideTangentCenter),
        -leftRadius,
        EPS);
    EXPECT_EQ(frustum.Contains(BoundingSphere(leftOutsideTangentCenter, leftRadius)),
        ContainsResult::Outside);

    const float farRadius = 0.83f;
    const Vec3f farInsideTangentCenter =
        MoveToSignedDistanceFromPlane(frustum.GetFar(), interiorPoint, farRadius);
    const Vec3f farOutsideTangentCenter =
        MoveToSignedDistanceFromPlane(frustum.GetFar(), interiorPoint, -farRadius);

    EXPECT_NEAR(SignedDistanceToPlane(frustum.GetFar(), farInsideTangentCenter), farRadius, EPS);
    EXPECT_EQ(frustum.Contains(BoundingSphere(farInsideTangentCenter, farRadius)),
        ContainsResult::Intersects);
    EXPECT_NEAR(SignedDistanceToPlane(frustum.GetFar(), farOutsideTangentCenter), -farRadius, EPS);
    EXPECT_EQ(frustum.Contains(BoundingSphere(farOutsideTangentCenter, farRadius)),
        ContainsResult::Outside);
}

TEST(Frustum, SelectionRectConstructor_ContainsOnlySelectedScreenRegion)
{
    const Camera camera = MakeFrustumCamera();
    const TrTransformf cameraXform = MakeFrustumCameraXform();
    const Frustum fullFrustum(camera, cameraXform);
    const Rect selectRect({ .X = 47, .Y = 41, .Width = 37, .Height = 29 });
    const Frustum selectionFrustum(camera, cameraXform, selectRect);

    const Vec3f selectedPoint =
        WorldFromScreenPoint(camera, cameraXform, Vec2f(65.5f, 55.5f), 0.43f);
    const Vec3f outsideSelectionPoint =
        WorldFromScreenPoint(camera, cameraXform, Vec2f(25.7f, 56.3f), 0.43f);

    EXPECT_EQ(fullFrustum.Contains(BoundingSphere(selectedPoint, 0.017f)), ContainsResult::Inside);
    EXPECT_EQ(selectionFrustum.Contains(BoundingSphere(selectedPoint, 0.017f)),
        ContainsResult::Inside);
    EXPECT_EQ(fullFrustum.Contains(BoundingSphere(outsideSelectionPoint, 0.017f)),
        ContainsResult::Inside);
    EXPECT_EQ(selectionFrustum.Contains(BoundingSphere(outsideSelectionPoint, 0.017f)),
        ContainsResult::Outside);
}

// NOLINTEND(readability-magic-numbers,cppcoreguidelines-avoid-magic-numbers)
