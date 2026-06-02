#pragma once
#include <span>
#include <cstdint>

#include "Vertex.h"

#include <vector>

class Shapes
{
public:

    class Geometry
    {
        friend class Shapes;

    public:

        std::span<const Vertex> GetVertices() const
        {
            return m_Vertices;
        }

        std::span<const VertexIndex> GetIndices() const
        {
            return m_Indices;
        }

        // Enable structured bindings
        template<std::size_t I>
        auto get() const noexcept -> std::conditional_t<I == 0, std::span<const Vertex>, std::span<const VertexIndex>>
        {
            static_assert(I < 2);
            if constexpr (I == 0) {return std::span(m_Vertices);}
            else if constexpr (I == 1) {return std::span(m_Indices);}
        }

    private:

        Geometry(std::vector<Vertex>&& vertices, std::vector<VertexIndex>&& indices) noexcept
            : m_Vertices(std::move(vertices))
            , m_Indices(std::move(indices))
        {
        }

        std::vector<Vertex> m_Vertices;
        std::vector<VertexIndex> m_Indices;
    };

    static constexpr float kMaxSmoothness = 10.0f;

    static Geometry Box(const float width, const float height, const float depth);

    // Smoothness controls tessellation (1-10, higher = smoother)
    static Geometry Ball(const float radius, const float requestedSmoothness);
    static Geometry Ball(const float radius)
    {
        return Ball(radius, kMaxSmoothness);
    }

    // Height along Y axis, centered at origin
    // Smoothness controls tessellation (1-10, higher = smoother)
    static Geometry Cylinder(const float height, const float radius, const float requestedSmoothness);
    static Geometry Cylinder(const float height, const float radius)
    {
        return Cylinder(height, radius, kMaxSmoothness);
    }

    // Generate a truncated cone with two radii.
    // radius1 = bottom radius, radius2 = top radius.
    // Height = 1.0, along Y axis, centered at origin.
    // Pass zero for one of the radii to produce a pure cone.
    static Geometry Cone(const float radius1, const float radius2, const float requestedSmoothness);
    static Geometry Cone(const float radius1, const float radius2)
    {
        return Cone(radius1, radius2, kMaxSmoothness);
    }

    // smoothness controls tessellation (1-10)
    // ringRadius > tubeRadius - Classic donut shape.
    // ringRadius == tubeRadius - Horn torus.
    // ringRadius < tubeRadius - Spindle torus (some geometry is overlapping).
    // ringRadius == 0 - Sphere of radius == tubeRadius.
    static Geometry Torus(const float ringRadius, const float tubeRadius, const float requestedSmoothness);
    static Geometry Torus(const float ringRadius, const float tubeRadius)
    {
        return Torus(ringRadius, tubeRadius, kMaxSmoothness);
    }
};

// Specializations to enable structured binding for Shapes::Geometry.
template<>
struct std::tuple_size<Shapes::Geometry> : std::integral_constant<std::size_t, 2> {};

template<>
struct std::tuple_element<0, Shapes::Geometry>
{
    using type = std::span<const Vertex>;
};

template<>
struct std::tuple_element<1, Shapes::Geometry>
{
    using type = std::span<const VertexIndex>;
};
