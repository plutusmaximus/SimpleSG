#pragma once

#include "Vertex.h"

#include <algorithm>
#include <span>

class BoundingSphere
{
public:

    BoundingSphere() = default;

    BoundingSphere(const Vec3f& center, const float radius) noexcept
        : m_Center(center)
        , m_Radius(radius)
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

    static BoundingSphere FromVertices(std::span<const Vertex> vertices);

private:
    Vec3f m_Center = {0.0f, 0.0f, 0.0f};
    float m_Radius = 0.0f;
};