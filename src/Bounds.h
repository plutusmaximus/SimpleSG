#pragma once

#include "Vertex.h"

#include <algorithm>
#include <span>

// Axis-aligned bounding box.
class AABoundingBox
{
public:

    AABoundingBox() = default;

    AABoundingBox(const Vec3f& min, const Vec3f& max) noexcept
        : m_Min(min)
        , m_Max(max)
    {
    }

    const Vec3f& GetMin() const { return m_Min; }
    const Vec3f& GetMax() const { return m_Max; }

    static AABoundingBox FromVertices(std::span<const Vertex> vertices,
        std::span<const VertexIndex> indices);

private:
    Vec3f m_Min = {0.0f, 0.0f, 0.0f};
    Vec3f m_Max = {0.0f, 0.0f, 0.0f};
};

class BoundingSphere
{
public:

    BoundingSphere() = default;

    BoundingSphere(const Vec3f& center, const float radius) noexcept
        : m_Center(center)
        , m_Radius(radius)
    {
    }

    explicit BoundingSphere(const AABoundingBox& aabb) noexcept
        : m_Center((aabb.GetMin() + aabb.GetMax()) * 0.5f)
        , m_Radius((aabb.GetMax() - m_Center).Length())
    {
    }

    float GetRadius() const
    {
        return m_Radius;
    }

    bool Contains(const Vec3f& point) const
    {
        const Vec3f v(point - m_Center);
        const double dist2 = v.Dot(v);
        const double r2 = m_Radius * m_Radius;
        // Use a tolerance that is always relative to the actual radius
        const double tol = 1e-6 * r2 + 1e-12; // 1e-12 for exact zero case
        return dist2 <= r2 + tol;
    }

    const Vec3f& GetCenter() const { return m_Center; }

private:
    Vec3f m_Center = {0.0f, 0.0f, 0.0f};
    float m_Radius = 0.0f;
};