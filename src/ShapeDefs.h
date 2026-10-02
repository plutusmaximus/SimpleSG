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
        float RingRadius{-1}; // Center to tube center. Must exceed TubeRadius.
        float TubeRadius{-1}; // Radius of the tube.
        float Smoothness{kMaxSmoothness}; // Controls tessellation (1-10, higher = smoother)
    };

    /// Each box face has a flat normal and fills the UV square.
    static MeshDef Box(const BoxParams& params);

    /// Uses a 5 by 4 atlas of separate triangular faces, in base face order.
    /// Each face fills the lower-left triangle of its cell with a small inset.
    /// Smoothness controls tessellation (1-10, higher = smoother).
    static MeshDef Ball(const BallParams& params);

    // Height along Y axis, centered at origin
    // Smoothness controls tessellation (1-10, higher = smoother)
    static MeshDef Cylinder(const CylinderParams& params);

    // Generate a truncated cone with two radii.
    // radius1 = bottom radius, radius2 = top radius.
    // Height = 1.0, along Y axis, centered at origin.
    // Pass zero for one of the radii to produce a pure cone.
    static MeshDef Cone(const ConeParams& params);

    /// Both UV directions wrap around the torus.
    /// Smoothness controls tessellation (1-10, higher = smoother).
    static MeshDef Torus(const TorusParams& params);
};
