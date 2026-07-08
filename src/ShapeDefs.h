#pragma once

#include "LevelDefs.h"

class ShapeDefs
{
public:

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

    static MeshDef Box(const BoxParams& params);

    // Smoothness controls tessellation (1-10, higher = smoother)
    static MeshDef Ball(const BallParams& params);

    // Height along Y axis, centered at origin
    // Smoothness controls tessellation (1-10, higher = smoother)
    static MeshDef Cylinder(const CylinderParams& params);

    // Generate a truncated cone with two radii.
    // radius1 = bottom radius, radius2 = top radius.
    // Height = 1.0, along Y axis, centered at origin.
    // Pass zero for one of the radii to produce a pure cone.
    static MeshDef Cone(const ConeParams& params);

    // smoothness controls tessellation (1-10)
    // ringRadius > tubeRadius - Classic donut shape.
    // ringRadius == tubeRadius - Horn torus.
    // ringRadius < tubeRadius - Spindle torus (some geometry is overlapping).
    // ringRadius == 0 - Sphere of radius == tubeRadius.
    static MeshDef Torus(const TorusParams& params);
};
