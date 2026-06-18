#pragma once

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

    // Translate a bounding box by an offset, producing a new bounding box.
    friend BoundingBox operator+(const BoundingBox& a, const Vec3f& offset) noexcept
    {
        BoundingBox result = a;
        result.m_Center += offset;
        return result;
    }

    // Compound merge two bounding boxes.
    BoundingBox& operator+=(const BoundingBox& other) noexcept { return *this = *this + other; }

    // Compound translate a bounding box by an offset.
    BoundingBox& operator+=(const Vec3f& offset) noexcept { return *this = *this + offset; }

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

    // Translate a bounding capsule by an offset, producing a new bounding capsule.
    friend BoundingCapsule operator+(const BoundingCapsule& a, const Vec3f& offset) noexcept
    {
        return BoundingCapsule(a.GetCenter() + offset, a.GetRadius(), a.GetHalfHeight());
    }

    // Compound translate a bounding capsule by an offset.
    BoundingCapsule& operator+=(const Vec3f& offset) noexcept { return *this = *this + offset; }

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

    // Translate a bounding sphere by an offset, producing a new bounding sphere.
    friend BoundingSphere operator+(const BoundingSphere& a, const Vec3f& offset) noexcept
    {
        return BoundingSphere(a.GetCenter() + offset, a.GetRadius());
    }

    // Compound merge two bounding spheres.
    BoundingSphere& operator+=(const BoundingSphere& other) noexcept
    {
        return *this = *this + other;
    }

    // Compound translate a bounding sphere by an offset.
    BoundingSphere& operator+=(const Vec3f& offset) noexcept { return *this = *this + offset; }

private:
    Vec3f m_Center;
    float m_Radius;
};