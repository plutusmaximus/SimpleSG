#pragma once

#include "Vertex.h"

#include <span>

class Box
{
public:

    Box() = default;

    explicit Box(const Vec3f& halfExtents) noexcept
        : m_HalfExtents(halfExtents)
    {
        MLG_ASSERT(halfExtents.x > 0.0f && halfExtents.y > 0.0f && halfExtents.z > 0.0f,
            "Box half extents must be positive");
    }

    /// @brief Construct a box that encompasses the two provided points.
    Box(const Vec3f& p0, const Vec3f& p1) noexcept
        : Box((GetMax(p0, p1) - GetMin(p0, p1)) * 0.5f)
    {
    }

    const Vec3f& GetHalfExtents() const { return m_HalfExtents; }

    static Box FromVertices(std::span<const Vertex> vertices, std::span<const VertexIndex> indices);

private:

    static Vec3f GetMin(const Vec3f& p0, const Vec3f& p1)
    {
        return Vec3f(
            std::min(p0.x, p1.x),
            std::min(p0.y, p1.y),
            std::min(p0.z, p1.z));
    }

    static Vec3f GetMax(const Vec3f& p0, const Vec3f& p1)
    {
        return Vec3f(
            std::max(p0.x, p1.x),
            std::max(p0.y, p1.y),
            std::max(p0.z, p1.z));
    }

    Vec3f m_HalfExtents{ 0 };
};

class Sphere
{
public:

    Sphere() = default;

    explicit Sphere(const float radius) noexcept
        : m_Radius(radius)
    {
        MLG_ASSERT(radius > 0.0f, "Sphere radius must be positive");
    }

    explicit Sphere(const Box& box) noexcept
        : m_Radius(box.GetHalfExtents().Length())
    {
        MLG_ASSERT(m_Radius > 0.0f, "Sphere radius must be positive");
    }

    float GetRadius() const { return m_Radius; }

private:
    float m_Radius{ 0 };
};

class Capsule
{
public:

    Capsule() = default;

    Capsule(const float radius, const float halfHeight)
        : m_Radius(radius), m_HalfHeight(halfHeight)
    {
        MLG_ASSERT(radius > 0.0f, "Capsule radius must be positive");
        MLG_ASSERT(halfHeight > 0.0f, "Capsule half height must be positive");
    }

    float GetRadius() const { return m_Radius; }
    float GetHalfHeight() const { return m_HalfHeight; }

private:
    float m_Radius{ 0 };
    float m_HalfHeight{ 0 };
};