#pragma once

#include "VecMath.h"
#include "Vertex.h"

#include <span>

class BoundingBox
{
public:
    BoundingBox() = delete;

    BoundingBox(const Vec3f& p0, const Vec3f& p1) noexcept;

    const Vec3f& GetCenter() const { return m_Center; }

    const Vec3f& GetHalfExtents() const { return m_HalfExtents; }

    // Merge two bounding boxes into a new bounding box that encompasses both.
    friend BoundingBox operator+(const BoundingBox& a, const BoundingBox& b) noexcept;

    friend BoundingBox operator*(const TrsTransformf& transform, const BoundingBox& a) noexcept
    {
        BoundingBox result = a;
        result.m_Center = transform * result.m_Center;
        return result;
    }

    friend BoundingBox operator*(const Mat44f& transform, const BoundingBox& a) noexcept
    {
        const Vec4f center4 = transform * Vec4f(a.m_Center, 1.0f);
        BoundingBox result = a;
        result.m_Center = Vec3f(center4.x, center4.y, center4.z);
        return result;
    }

    // Compound merge two bounding boxes.
    BoundingBox& operator+=(const BoundingBox& other) noexcept { return *this = *this + other; }

    static BoundingBox FromVertices(std::span<const Vertex> vertices,
        std::span<const VertexIndex> indices);

private:
    Vec3f m_Center;
    Vec3f m_HalfExtents;
};

class BoundingCapsule
{
public:
    BoundingCapsule() = delete;

    BoundingCapsule(const Vec3f& center, const float radius, const float halfHeight) noexcept
        : m_Center(center),
          m_Radius(radius),
          m_HalfHeight(halfHeight)
    {
        MLG_REQUIRE(radius > 0.0f, "Bounding capsule radius must be positive");
        MLG_REQUIRE(halfHeight > 0.0f, "Bounding capsule half height must be positive");
    }

    const Vec3f& GetCenter() const { return m_Center; }

    float GetRadius() const { return m_Radius; }

    float GetHalfHeight() const { return m_HalfHeight; }

    friend BoundingCapsule operator*(const TrsTransformf& transform, const BoundingCapsule& a) noexcept
    {
        BoundingCapsule result = a;
        result.m_Center = transform * result.m_Center;
        return result;
    }

    friend BoundingCapsule operator*(const Mat44f& transform, const BoundingCapsule& a) noexcept
    {
        const Vec4f center4 = transform * Vec4f(a.m_Center, 1.0f);
        BoundingCapsule result = a;
        result.m_Center = Vec3f(center4.x, center4.y, center4.z);
        return result;
    }

private:
    Vec3f m_Center;
    float m_Radius;
    float m_HalfHeight;
};

class BoundingSphere
{
public:
    BoundingSphere() = delete;

    BoundingSphere(const Vec3f& center, const float radius) noexcept
        : m_Center(center),
          m_Radius(radius)
    {
        MLG_REQUIRE(radius > 0.0f, "Bounding sphere radius must be positive");
    }

    explicit BoundingSphere(const BoundingBox& bbox)
        : m_Center(bbox.GetCenter()),
          m_Radius(bbox.GetHalfExtents().Length())
    {
    }

    explicit BoundingSphere(const BoundingCapsule& capsule)
        : m_Center(capsule.GetCenter()),
          m_Radius(capsule.GetRadius() + capsule.GetHalfHeight())
    {
    }

    const Vec3f& GetCenter() const { return m_Center; }

    float GetRadius() const { return m_Radius; }

    // Merge two bounding spheres into a new bounding sphere that encompasses both.
    friend BoundingSphere operator+(const BoundingSphere& a, const BoundingSphere& b) noexcept;

    friend BoundingSphere operator*(const TrsTransformf& transform, const BoundingSphere& a) noexcept
    {
        BoundingSphere result = a;
        result.m_Center = transform * result.m_Center;
        return result;
    }

    friend BoundingSphere operator*(const Mat44f& transform, const BoundingSphere& a) noexcept
    {
        const Vec4f center4 = transform * Vec4f(a.m_Center, 1.0f);
        BoundingSphere result = a;
        result.m_Center = Vec3f(center4.x, center4.y, center4.z);
        return result;
    }

    // Compound merge two bounding spheres.
    BoundingSphere& operator+=(const BoundingSphere& other) noexcept
    {
        return *this = *this + other;
    }

private:
    Vec3f m_Center;
    float m_Radius;
};