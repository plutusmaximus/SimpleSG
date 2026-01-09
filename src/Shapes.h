#pragma once
#include <span>
#include <cstdint>

#include "Vertex.h"
#include "imvector.h"

class Shapes
{
public:

    class Geometry
    {
        friend class Shapes;

    public:

        const imvector<Vertex> GetVertices() const
        {
            return m_Vertices;
        }

        const imvector<VertexIndex> GetIndices() const
        {
            return m_Indices;
        }
        
        // Enable structured bindings
        template<std::size_t I>
        decltype(auto) get() const
        {
            if constexpr (I == 0) return (m_Vertices);
            else if constexpr (I == 1) return (m_Indices);
        }

    private:

        // Must use std::move() at the call site to avoid copies.
        // If std::move() is not used the compiler will throw an error similar to:
        // "no overloaded function could convert all the argument types"
        Geometry(const imvector<Vertex>& vertices, const imvector<VertexIndex>& indices)
            : m_Vertices(vertices)
            , m_Indices(indices)
        {
        }

        imvector<Vertex> m_Vertices;
        imvector<uint32_t> m_Indices;
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
    using type = imvector<Vertex>;
};

template<>
struct std::tuple_element<1, Shapes::Geometry>
{
    using type = imvector<uint32_t>;
};
