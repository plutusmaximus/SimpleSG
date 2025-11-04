#pragma once
#include <vector>
#include <cstdint>

#include "Vertex.h"

// Generate a box with specified dimensions
// Width = X axis, Height = Y axis, Depth = Z axis
void MakeBox(const float width, const float height, const float depth,
    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

// Generate a sphere with specified diameter and smoothness
// Smoothness controls tessellation (1-10, higher = smoother)
void MakeBall(const float diameter, const float smoothness,
    std::vector<Vertex>& vertices, std::vector<VertexIndex>& indices);

// Generate a cylinder with specified height, diameter and smoothness
// Height along Y axis, centered at origin
void MakeCylinder(const float height, const float diameter, const float smoothness,
    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

// Generate a truncated cone with two diameters
// diameter1 = bottom diameter, diameter2 = top diameter
// Height = 1.0, along Y axis, centered at origin
void MakeCone(const float diameter1, const float diameter2, const float smoothness,
    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);

// Generate a torus (donut shape)
// majorDiameter = diameter of the ring (center to center)
// minorDiameter = diameter of the tube
// smoothness controls tessellation (1-10)
void MakeTorus(const float majorDiameter, const float minorDiameter, const float smoothness,
    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices);