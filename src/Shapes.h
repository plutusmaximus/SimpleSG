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
            if constexpr (I == 0) return std::span<const Vertex>(m_Vertices);
            else if constexpr (I == 1) return std::span<const VertexIndex>(m_Indices);
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

    static Geometry Box(const float width, const float height, const float depth);

    // Smoothness controls tessellation (1-10, higher = smoother)
    static Geometry Ball(const float diameter, const float smoothness);

    // Height along Y axis, centered at origin
    // Smoothness controls tessellation (1-10, higher = smoother)
    static Geometry Cylinder(const float height, const float diameter, const float smoothness);

    // Generate a truncated cone with two diameters.
    // diameter1 = bottom diameter, diameter2 = top diameter.
    // Height = 1.0, along Y axis, centered at origin.
    // Pass zero for one of the diameters to produce a pure cone.
    static Geometry Cone(const float diameter1, const float diameter2, const float smoothness);

    // smoothness controls tessellation (1-10)
    // ringDiameter > tubeDiameter - Classic donut shape.
    // ringDiameter == tubeDiameter - Horn torus.
    // ringDiameter < tubeDiameter - Spindle torus (some geometry is overlapping).
    // ringDiameter == 0 - Sphere of radius == tubeRadius.
    static Geometry Torus(const float ringDiameter, const float tubeDiameter, const float smoothness);
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
