#pragma once
#include <vector>
#include <cstdint>

#include "Vertex.h"

class Shapes
{
public:

    class Geometry
    {
        friend class Shapes;

    public:

        const std::vector<Vertex>& GetVertices() const
        {
            return m_Vertices;
        }

        const std::vector<VertexIndex>& GetIndices() const
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
        Geometry(std::vector<Vertex>&& vertices, std::vector<VertexIndex>&& indices)
            : m_Vertices(std::move(vertices))
            , m_Indices(std::move(indices))
        {
        }

        std::vector<Vertex> m_Vertices;
        std::vector<uint32_t> m_Indices;
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

    // majorDiameter = diameter of the ring (center to center)
    // minorDiameter = diameter of the tube
    // smoothness controls tessellation (1-10)
    static Geometry Torus(const float majorDiameter, const float minorDiameter, const float smoothness);
};

// Specializations to enable structured binding for Shapes::Geometry.
namespace std
{
    template<>
    struct tuple_size<Shapes::Geometry> : std::integral_constant<std::size_t, 2> {};

    template<>
    struct tuple_element<0, Shapes::Geometry>
    {
        using type = std::vector<Vertex>;
    };

    template<>
    struct tuple_element<1, Shapes::Geometry>
    {
        using type = std::vector<uint32_t>;
    };
}