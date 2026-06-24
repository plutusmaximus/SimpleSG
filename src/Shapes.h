#pragma once
#include <span>

#include "Vertex.h"

#include <vector>

class Shapes
{
public:

    class Geometry
    {
        friend class Shapes;

    public:

        Geometry() = delete;

        std::span<const Vertex> GetVertices() const
        {
            return m_Vertices;
        }

        std::span<const VertexIndex> GetIndices() const
        {
            return m_Indices;
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

    struct BoxParams
    {
        float Width;
        float Height;
        float Depth;
    };

    struct BallParams
    {
        float Radius{-1};
        float Smoothness{kMaxSmoothness}; // Controls tessellation (1-10, higher = smoother)
    };

    struct CylinderParams
    {
        float Height{-1};
        float Radius{-1};
        float Smoothness{kMaxSmoothness}; // Controls tessellation (1-10, higher = smoother)
    };

    struct ConeParams
    {
        float Radius1{-1}; // Bottom radius
        float Radius2{-1}; // Top radius. Pass zero for a pure cone.
        float Smoothness{kMaxSmoothness}; // Controls tessellation (1-10, higher = smoother)
    };

    struct TorusParams
    {
        float RingRadius{-1}; // Distance from center of tube to center of torus. Pass zero for a sphere.
        float TubeRadius{-1}; // Radius of the tube.
        float Smoothness{kMaxSmoothness}; // Controls tessellation (1-10, higher = smoother)
    };

    static Geometry Box(const BoxParams& params);

    // Smoothness controls tessellation (1-10, higher = smoother)
    static Geometry Ball(const BallParams& params);

    // Height along Y axis, centered at origin
    // Smoothness controls tessellation (1-10, higher = smoother)
    static Geometry Cylinder(const CylinderParams& params);

    // Generate a truncated cone with two radii.
    // radius1 = bottom radius, radius2 = top radius.
    // Height = 1.0, along Y axis, centered at origin.
    // Pass zero for one of the radii to produce a pure cone.
    static Geometry Cone(const ConeParams& params);

    // smoothness controls tessellation (1-10)
    // ringRadius > tubeRadius - Classic donut shape.
    // ringRadius == tubeRadius - Horn torus.
    // ringRadius < tubeRadius - Spindle torus (some geometry is overlapping).
    // ringRadius == 0 - Sphere of radius == tubeRadius.
    static Geometry Torus(const TorusParams& params);
};
