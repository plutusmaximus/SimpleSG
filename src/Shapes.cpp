#include "Shapes.h"
#include <cmath>
#include <algorithm>
#include <numbers>

static constexpr float M_PI = std::numbers::pi_v<float>;

Shapes::Geometry
Shapes::Box(const float width, const float height, const float depth)
{
    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    const float hw = width * 0.5f;
    const float hh = height * 0.5f;
    const float hd = depth * 0.5f;

    // 8 vertices - one per corner
    vertices.reserve(8);
    indices.reserve(36);

    // Calculate normalized normals for each corner (average of 3 adjacent faces)
    const float invSqrt3 = 1.0f / std::sqrt(3.0f);

    // Vertex order:
    // 0: (-x, -y, -z)  1: (+x, -y, -z)
    // 2: (+x, +y, -z)  3: (-x, +y, -z)
    // 4: (-x, -y, +z)  5: (+x, -y, +z)
    // 6: (+x, +y, +z)  7: (-x, +y, +z)

    vertices.push_back({ { -hw, -hh, -hd }, { -invSqrt3, -invSqrt3, -invSqrt3 } });
    vertices.push_back({ {  hw, -hh, -hd }, {  invSqrt3, -invSqrt3, -invSqrt3 } });
    vertices.push_back({ {  hw,  hh, -hd }, {  invSqrt3,  invSqrt3, -invSqrt3 } });
    vertices.push_back({ { -hw,  hh, -hd }, { -invSqrt3,  invSqrt3, -invSqrt3 } });
    vertices.push_back({ { -hw, -hh,  hd }, { -invSqrt3, -invSqrt3,  invSqrt3 } });
    vertices.push_back({ {  hw, -hh,  hd }, {  invSqrt3, -invSqrt3,  invSqrt3 } });
    vertices.push_back({ {  hw,  hh,  hd }, {  invSqrt3,  invSqrt3,  invSqrt3 } });
    vertices.push_back({ { -hw,  hh,  hd }, { -invSqrt3,  invSqrt3,  invSqrt3 } });

    // Front face (+Z) - clockwise from front
    indices.push_back(4); indices.push_back(5); indices.push_back(6);
    indices.push_back(4); indices.push_back(6); indices.push_back(7);

    // Back face (-Z) - clockwise from back
    indices.push_back(1); indices.push_back(0); indices.push_back(3);
    indices.push_back(1); indices.push_back(3); indices.push_back(2);

    // Right face (+X) - clockwise from right
    indices.push_back(5); indices.push_back(1); indices.push_back(2);
    indices.push_back(5); indices.push_back(2); indices.push_back(6);

    // Left face (-X) - clockwise from left
    indices.push_back(0); indices.push_back(4); indices.push_back(7);
    indices.push_back(0); indices.push_back(7); indices.push_back(3);

    // Top face (+Y) - clockwise from top
    indices.push_back(7); indices.push_back(6); indices.push_back(2);
    indices.push_back(7); indices.push_back(2); indices.push_back(3);

    // Bottom face (-Y) - clockwise from bottom
    indices.push_back(0); indices.push_back(1); indices.push_back(5);
    indices.push_back(0); indices.push_back(5); indices.push_back(4);

    return Geometry{ std::move(vertices), std::move(indices) };
}

Shapes::Geometry
Shapes::Ball(const float diameter, const float smoothness)
{
    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    const float radius = diameter * 0.5f;

    // Clamp smoothness to determine subdivision level
    const float s = std::max(1.0f, std::min(10.0f, smoothness));
    const int subdivisions = static_cast<int>(s * 0.3f); // 0 to 3 subdivisions

    // Calculate exact final sizes
    const int finalTriangles = 20 * (1 << (2 * subdivisions)); // 20 * 4^subdivisions
    const int finalIndices = finalTriangles * 3;

    // Calculate exact vertex count using closed-form formula
    // Base: 12 vertices
    // Subdivisions add: 60 * (4^subdivisions - 1) / 3 vertices
    const int addedVertices = subdivisions > 0 ? 60 * ((1 << (2 * subdivisions)) - 1) / 3 : 0;
    const int totalVertices = 12 + addedVertices;

    vertices.reserve(totalVertices);
    indices.reserve(finalIndices);

    // Create icosahedron base vertices
    const float t = (1.0f + std::sqrt(5.0f)) / 2.0f;
    const float len = std::sqrt(1.0f + t * t);
    const float a = 1.0f / len;
    const float b = t / len;

    // 12 vertices of icosahedron
    vertices.push_back({ { -a,  b,  0 }, { -a,  b,  0 } });
    vertices.push_back({ {  a,  b,  0 }, {  a,  b,  0 } });
    vertices.push_back({ { -a, -b,  0 }, { -a, -b,  0 } });
    vertices.push_back({ {  a, -b,  0 }, {  a, -b,  0 } });
    vertices.push_back({ {  0, -a,  b }, {  0, -a,  b } });
    vertices.push_back({ {  0,  a,  b }, {  0,  a,  b } });
    vertices.push_back({ {  0, -a, -b }, {  0, -a, -b } });
    vertices.push_back({ {  0,  a, -b }, {  0,  a, -b } });
    vertices.push_back({ {  b,  0, -a }, {  b,  0, -a } });
    vertices.push_back({ {  b,  0,  a }, {  b,  0,  a } });
    vertices.push_back({ { -b,  0, -a }, { -b,  0, -a } });
    vertices.push_back({ { -b,  0,  a }, { -b,  0,  a } });

    // 20 faces of icosahedron (clockwise winding)
    const VertexIndex faces[][3] =
    {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
    };

    for (int i = 0; i < 20; ++i)
    {
        indices.push_back(faces[i][0]);
        indices.push_back(faces[i][1]);
        indices.push_back(faces[i][2]);
    }

    // Pre-allocate newIndices with final size
    std::vector<uint32_t> newIndices;
    newIndices.reserve(finalIndices);

    // Subdivide triangles
    for (int subdiv = 0; subdiv < subdivisions; ++subdiv)
    {
        newIndices.clear();

        for (size_t i = 0; i < indices.size(); i += 3)
        {
            const VertexIndex v0 = indices[i];
            const VertexIndex v1 = indices[i + 1];
            const VertexIndex v2 = indices[i + 2];

            // Get midpoints
            VertexPos mid01
            {
                (vertices[v0].pos.x + vertices[v1].pos.x) * 0.5f,
                (vertices[v0].pos.y + vertices[v1].pos.y) * 0.5f,
                (vertices[v0].pos.z + vertices[v1].pos.z) * 0.5f
            };
            VertexPos mid12
            {
                (vertices[v1].pos.x + vertices[v2].pos.x) * 0.5f,
                (vertices[v1].pos.y + vertices[v2].pos.y) * 0.5f,
                (vertices[v1].pos.z + vertices[v2].pos.z) * 0.5f
            };
            VertexPos mid20
            {
                (vertices[v2].pos.x + vertices[v0].pos.x) * 0.5f,
                (vertices[v2].pos.y + vertices[v0].pos.y) * 0.5f,
                (vertices[v2].pos.z + vertices[v0].pos.z) * 0.5f
            };

            // Normalize to sphere
            const float len01 = std::sqrt(mid01.x * mid01.x + mid01.y * mid01.y + mid01.z * mid01.z);
            const float len12 = std::sqrt(mid12.x * mid12.x + mid12.y * mid12.y + mid12.z * mid12.z);
            const float len20 = std::sqrt(mid20.x * mid20.x + mid20.y * mid20.y + mid20.z * mid20.z);

            mid01.x /= len01; mid01.y /= len01; mid01.z /= len01;
            mid12.x /= len12; mid12.y /= len12; mid12.z /= len12;
            mid20.x /= len20; mid20.y /= len20; mid20.z /= len20;

            const VertexIndex m01 = vertices.size();
            vertices.push_back({ mid01, {mid01.x, mid01.y, mid01.z} });
            const VertexIndex m12 = vertices.size();
            vertices.push_back({ mid12, {mid12.x, mid12.y, mid12.z} });
            const VertexIndex m20 = vertices.size();
            vertices.push_back({ mid20, {mid20.x, mid20.y, mid20.z} });

            // Create 4 new triangles
            newIndices.push_back(v0);  newIndices.push_back(m01); newIndices.push_back(m20);
            newIndices.push_back(v1);  newIndices.push_back(m12); newIndices.push_back(m01);
            newIndices.push_back(v2);  newIndices.push_back(m20); newIndices.push_back(m12);
            newIndices.push_back(m01); newIndices.push_back(m12); newIndices.push_back(m20);
        }

        std::swap(indices, newIndices);
    }

    // Scale to desired radius
    for (auto& v : vertices)
    {
        v.pos.x *= radius;
        v.pos.y *= radius;
        v.pos.z *= radius;
    }

    return Geometry{ std::move(vertices), std::move(indices) };
}

Shapes::Geometry
Shapes::Cylinder(const float height, const float diameter, const float smoothness)
{
    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    const float radius = diameter * 0.5f;
    const float halfHeight = height * 0.5f;

    // Clamp smoothness and calculate segments
    const float s = std::max(1.0f, std::min(10.0f, smoothness));
    const uint32_t segments = static_cast<uint32_t>(8 + s * 4); // 12 to 48 segments

    vertices.reserve(segments * 2 + 2);
    indices.reserve(segments * 12);

    // Side vertices (top and bottom rings)
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const float theta = 2.0f * M_PI * static_cast<float>(seg) / static_cast<float>(segments);
        const float x = radius * std::cos(theta);
        const float z = radius * std::sin(theta);

        const VertexNormal normal = VertexNormal( x / radius, 0.0f, z / radius ).Normalize();

        // Bottom vertex
        vertices.push_back({ { x, -halfHeight, z }, normal });

        // Top vertex
        vertices.push_back({ { x, halfHeight, z }, normal });
    }

    // Generate side indices
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        uint32_t current = seg * 2;
        uint32_t next = ((seg + 1) % segments) * 2;

        // First triangle (clockwise)
        indices.push_back(current);
        indices.push_back(current + 1);
        indices.push_back(next);

        // Second triangle (clockwise)
        indices.push_back(next);
        indices.push_back(current + 1);
        indices.push_back(next + 1);
    }

    // Cap centers
    uint32_t bottomCenter = segments * 2;
    uint32_t topCenter = bottomCenter + 1;

    vertices.push_back({ { 0.0f, -halfHeight, 0.0f }, { 0.0f, -1.0f, 0.0f } });
    vertices.push_back({ { 0.0f, halfHeight, 0.0f }, { 0.0f, 1.0f, 0.0f } });

    // Bottom cap indices (clockwise from below, using side vertices)
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        uint32_t current = seg * 2;
        uint32_t next = ((seg + 1) % segments) * 2;

        indices.push_back(bottomCenter);
        indices.push_back(current);
        indices.push_back(next);
    }

    // Top cap indices (clockwise from above, using side vertices)
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        uint32_t current = seg * 2 + 1;
        uint32_t next = ((seg + 1) % segments) * 2 + 1;

        indices.push_back(topCenter);
        indices.push_back(next);
        indices.push_back(current);
    }

    return Geometry{ std::move(vertices), std::move(indices) };
}

Shapes::Geometry
Shapes::Cone(const float diameter1, const float diameter2, const float smoothness)
{
    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    float radius1 = diameter1 * 0.5f; // Bottom radius
    float radius2 = diameter2 * 0.5f; // Top radius
    float height = 1.0f;
    float halfHeight = height * 0.5f;

    // Clamp smoothness and calculate segments
    float s = std::max(1.0f, std::min(10.0f, smoothness));
    uint32_t segments = static_cast<uint32_t>(8 + s * 4); // 12 to 48 segments

    vertices.reserve(segments * 2 + 2);
    indices.reserve(segments * 12);

    // Calculate slant normal
    float dr = radius2 - radius1;
    float slantLength = std::sqrt(dr * dr + height * height);
    float normalY = dr / slantLength;
    float normalXZ = height / slantLength;

    // Side vertices
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        float theta = 2.0f * M_PI * static_cast<float>(seg) / static_cast<float>(segments);
        float cosTheta = std::cos(theta);
        float sinTheta = std::sin(theta);

        float x1 = radius1 * cosTheta;
        float z1 = radius1 * sinTheta;
        float x2 = radius2 * cosTheta;
        float z2 = radius2 * sinTheta;

        const VertexNormal normal = VertexNormal{ cosTheta * normalXZ, normalY, sinTheta * normalXZ }.Normalize();

        // Bottom vertex
        vertices.push_back({ { x1, -halfHeight, z1 }, normal });

        // Top vertex
        vertices.push_back({ { x2, halfHeight, z2 }, normal });
    }

    // Generate side indices
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const uint32_t current = seg * 2;
        const uint32_t next = ((seg + 1) % segments) * 2;

        // First triangle (clockwise)
        indices.push_back(current);
        indices.push_back(current + 1);
        indices.push_back(next);

        // Second triangle (clockwise) - only if both radii are non-zero
        if (radius1 > 0.0f && radius2 > 0.0f)
        {
            indices.push_back(next);
            indices.push_back(current + 1);
            indices.push_back(next + 1);
        }
    }

    const uint32_t bottomCenter = segments * 2;
    const uint32_t topCenter = bottomCenter + 1;

    // Bottom cap (only if radius1 > 0)
    if (radius1 > 0.0f)
    {
        vertices.push_back({ { 0.0f, -halfHeight, 0.0f }, { 0.0f, -1.0f, 0.0f } });

        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            const uint32_t current = seg * 2;
            const uint32_t next = ((seg + 1) % segments) * 2;

            indices.push_back(bottomCenter);
            indices.push_back(current);
            indices.push_back(next);
        }
    }

    // Top cap (only if radius2 > 0)
    if (radius2 > 0.0f)
    {
        vertices.push_back({ { 0.0f, halfHeight, 0.0f }, { 0.0f, 1.0f, 0.0f } });

        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            const uint32_t current = seg * 2 + 1;
            const uint32_t next = ((seg + 1) % segments) * 2 + 1;

            indices.push_back(topCenter);
            indices.push_back(next);
            indices.push_back(current);
        }
    }

    return Geometry{ std::move(vertices), std::move(indices) };
}

Shapes::Geometry
Shapes::Torus(
    const float majorDiameter,
    const float minorDiameter,
    const float smoothness)
{
    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    const float majorRadius = majorDiameter * 0.5f;
    const float minorRadius = minorDiameter * 0.5f;

    // Minimum smoothness of 3 to avoid degenerate geometry
    const uint32_t numSegmentsMajor = std::max(3u, static_cast<uint32_t>(smoothness * 4));
    const uint32_t numSegmentsMinor = std::max(3u, static_cast<uint32_t>(smoothness * 4));

    const float dTheta = 2.0f * M_PI / static_cast<float>(numSegmentsMajor);
    const float dPhi = 2.0f * M_PI / static_cast<float>(numSegmentsMinor);

    // Reserve memory to avoid reallocations
    vertices.reserve(numSegmentsMajor * numSegmentsMinor);
    indices.reserve(numSegmentsMajor * numSegmentsMinor * 6);

    // --- Generate vertices ---
    for (uint32_t i = 0; i < numSegmentsMajor; ++i)
    {
        const float theta = i * dTheta;
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);

        for (uint32_t j = 0; j < numSegmentsMinor; ++j)
        {
            const float phi = j * dPhi;
            const float cosPhi = std::cos(phi);
            const float sinPhi = std::sin(phi);

            // Vertex position (left-handed)
            const float x = (majorRadius + minorRadius * cosPhi) * cosTheta;
            const float y = (majorRadius + minorRadius * cosPhi) * sinTheta;
            const float z = minorRadius * sinPhi;

            // Unit normal
            const float nx = cosTheta * cosPhi;
            const float ny = sinTheta * cosPhi;
            const float nz = sinPhi;

            vertices.push_back(Vertex{
                { x, y, z },
                { nx, ny, nz }
                });
        }
    }

    // --- Generate triangle indices (CLOCKWISE for left-handed system) ---
    for (uint32_t i = 0; i < numSegmentsMajor; ++i)
    {
        const uint32_t nextI = (i + 1) % numSegmentsMajor;
        for (uint32_t j = 0; j < numSegmentsMinor; ++j)
        {
            const uint32_t nextJ = (j + 1) % numSegmentsMinor;

            const uint32_t i0 = i * numSegmentsMinor + j;
            const uint32_t i1 = nextI * numSegmentsMinor + j;
            const uint32_t i2 = nextI * numSegmentsMinor + nextJ;
            const uint32_t i3 = i * numSegmentsMinor + nextJ;

            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    return Geometry{ std::move(vertices), std::move(indices) };
}