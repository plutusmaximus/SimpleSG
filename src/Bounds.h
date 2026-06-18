#pragma once

#include "Vertex.h"

#include <span>

class Box
{
public:

    Box() = delete;

    explicit Box(const Vec3f& halfExtents) noexcept
        : m_HalfExtents(halfExtents)
    {
        MLG_ASSERT(halfExtents.x >= 0.0f && halfExtents.y >= 0.0f && halfExtents.z >= 0.0f,
            "Box half extents must not be negative");
        MLG_ASSERT(halfExtents.x > 0.0f || halfExtents.y > 0.0f || halfExtents.z > 0.0f,
            "At least one dimension of box extents must be greater than zero");
    }

    /// @brief Construct a box that encompasses the two provided points.
    Box(const Vec3f& p0, const Vec3f& p1) noexcept
        : Box((GetMax(p0, p1) - GetMin(p0, p1)) * 0.5f)
    {
    }

    const Vec3f& GetHalfExtents() const { return m_HalfExtents; }

    float GetSphereRadius() const
    {
        return std::max({m_HalfExtents.x, m_HalfExtents.y, m_HalfExtents.z});
    }

    [[nodiscard]] Box Combine(const Box& other) const
    {
        const float minX = std::min(-m_HalfExtents.x, -other.m_HalfExtents.x);
        const float minY = std::min(-m_HalfExtents.y, -other.m_HalfExtents.y);
        const float minZ = std::min(-m_HalfExtents.z, -other.m_HalfExtents.z);
        const float maxX = std::max(m_HalfExtents.x, other.m_HalfExtents.x);
        const float maxY = std::max(m_HalfExtents.y, other.m_HalfExtents.y);
        const float maxZ = std::max(m_HalfExtents.z, other.m_HalfExtents.z);

        const Vec3f halfExtents((maxX - minX) * 0.5f, (maxY - minY) * 0.5f, (maxZ - minZ) * 0.5f);
        return Box(halfExtents);
    }

    static Box FromVertices(std::span<const Vertex> vertices, std::span<const VertexIndex> indices);

private:
    static Vec3f GetMin(const Vec3f& p0, const Vec3f& p1)
    {
        return //
            {
                std::min(p0.x, p1.x),
                std::min(p0.y, p1.y),
                std::min(p0.z, p1.z),
            };
    }

    static Vec3f GetMax(const Vec3f& p0, const Vec3f& p1)
    {
        return //
            {
                std::max(p0.x, p1.x),
                std::max(p0.y, p1.y),
                std::max(p0.z, p1.z),
            };
    }

    Vec3f m_HalfExtents;
};

class Sphere
{
public:

    Sphere() = delete;

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
    float m_Radius;
};

class Capsule
{
public:

    Capsule() = delete;

    Capsule(const float radius, const float halfHeight)
        : m_Radius(radius), m_HalfHeight(halfHeight)
    {
        MLG_ASSERT(radius > 0.0f, "Capsule radius must be positive");
        MLG_ASSERT(halfHeight > 0.0f, "Capsule half height must be positive");
    }

    float GetRadius() const { return m_Radius; }
    float GetHalfHeight() const { return m_HalfHeight; }

    float GetSphereRadius() const { return m_HalfHeight + m_Radius; }

private:
    float m_Radius;
    float m_HalfHeight;
};

class BoundingBox
{
public:
    BoundingBox() = delete;

    constexpr BoundingBox(const Vec3f& p0, const Vec3f& p1) noexcept
        : m_Center(p0 + ((p1 - p0) * 0.5f)),
          m_HalfExtents(ComputeHalfExtents(p0, p1))
    {
    }

    constexpr const Vec3f& GetCenter() const { return m_Center; }

    constexpr const Vec3f& GetHalfExtents() const { return m_HalfExtents; }

    [[nodiscard]] constexpr BoundingBox Combine(const BoundingBox& other) const
    {
        const Vec3f minA = GetCenter() - GetHalfExtents();
        const Vec3f maxA = GetCenter() + GetHalfExtents();
        const Vec3f minB = other.GetCenter() - other.GetHalfExtents();
        const Vec3f maxB = other.GetCenter() + other.GetHalfExtents();
        
        const Vec3f min = ComputeMin(minA, minB);
        const Vec3f max = ComputeMax(maxA, maxB);

        return BoundingBox(min, max);
    }

    static BoundingBox FromVertices(std::span<const Vertex> vertices,
        std::span<const VertexIndex> indices);

private:
    static Vec3f ComputeMin(const Vec3f& p0, const Vec3f& p1)
    {
        return //
            {
                std::min(p0.x, p1.x),
                std::min(p0.y, p1.y),
                std::min(p0.z, p1.z),
            };
    }

    static Vec3f ComputeMax(const Vec3f& p0, const Vec3f& p1)
    {
        return //
            {
                std::max(p0.x, p1.x),
                std::max(p0.y, p1.y),
                std::max(p0.z, p1.z),
            };
    }

    static Vec3f ComputeHalfExtents(const Vec3f& p0, const Vec3f& p1)
    {
        const Vec3f min = ComputeMin(p0, p1);
        const Vec3f max = ComputeMax(p0, p1);
        return (max - min) * 0.5f;
    }

    Vec3f m_Center;
    Vec3f m_HalfExtents;
};

class BoundingSphere
{
public:
    BoundingSphere() = delete;

    constexpr BoundingSphere(const Vec3f& center, const float radius) noexcept
        : m_Center(center),
          m_Radius(radius)
    {
    }

    constexpr explicit BoundingSphere(const BoundingBox& bbox)
        : m_Center(bbox.GetCenter()),
          m_Radius(bbox.GetHalfExtents().Length())
    {
    }

    constexpr const Vec3f& GetCenter() const { return m_Center; }

    constexpr float GetRadius() const { return m_Radius; }

private:

    Vec3f m_Center;
    float m_Radius;
};