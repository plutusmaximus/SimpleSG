#pragma once

#include "Vertex.h"

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

    const Vec3f& GetCenter() const { return m_Center; }

    static BoundingSphere FromVertices(std::span<const Vertex> vertices);

private:
    Vec3f m_Center = {0.0f, 0.0f, 0.0f};
    float m_Radius = 0.0f;
};