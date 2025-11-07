#include "Shapes.h"
#include <cmath>
#include <algorithm>
#include <numbers>

#include "Error.h"

static constexpr float M_PI = std::numbers::pi_v<float>;

Shapes::Geometry
Shapes::Box(const float width, const float height, const float depth)
{
    eassert(width > 0);
    eassert(height > 0);
    eassert(depth > 0);

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
    eassert(diameter > 0);
    eassert(smoothness > 0);

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    const float radius = diameter * 0.5f;

    // Clamp smoothness to determine subdivision level
    const float s = std::max(1.0f, std::min(10.0f, smoothness));
    const int subdivisions = static_cast<int>(s * 0.3f); // 0 to 3 subdivisions

    // Calculate exact final sizes with deduplication
    // With deduplication, vertices are shared between triangles
    // Formula: V = 10 * 4^n + 2 (for n subdivisions)
    const int finalTriangles = 20 * (1 << (2 * subdivisions)); // 20 * 4^subdivisions
    const int finalIndices = finalTriangles * 3;
    const int totalVertices = subdivisions > 0 ? 10 * (1 << (2 * subdivisions)) + 2 : 12;

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

    // Hoist midpoint cache outside the loop
    std::unordered_map<uint64_t, VertexIndex> midpointCache;

    // Lambda to get or create midpoint vertex
    auto getMidpoint = [&](VertexIndex v0, VertexIndex v1) -> VertexIndex {
        // Ensure consistent ordering for the key
        if (v0 > v1) std::swap(v0, v1);
        uint64_t key = (static_cast<uint64_t>(v0) << 32) | v1;

        auto it = midpointCache.find(key);
        if (it != midpointCache.end()) {
            return it->second;
        }

        // Calculate new midpoint
        VertexPos mid{
            (vertices[v0].pos.x + vertices[v1].pos.x) * 0.5f,
            (vertices[v0].pos.y + vertices[v1].pos.y) * 0.5f,
            (vertices[v0].pos.z + vertices[v1].pos.z) * 0.5f
        };

        // Normalize to sphere
        const float len = std::sqrt(mid.x * mid.x + mid.y * mid.y + mid.z * mid.z);
        mid.x /= len; mid.y /= len; mid.z /= len;

        VertexIndex newIdx = vertices.size();
        vertices.push_back({ mid, {mid.x, mid.y, mid.z} });
        midpointCache[key] = newIdx;
        return newIdx;
    };

    // Subdivide triangles with vertex deduplication
    for (int subdiv = 0; subdiv < subdivisions; ++subdiv)
    {
        midpointCache.clear(); // Clear cache for each subdivision level

        const size_t currentTriangleCount = indices.size() / 3;
        const size_t newIndexCount = currentTriangleCount * 12; // Each triangle becomes 4 triangles (12 indices)

        // Resize to final size for this iteration
        indices.resize(newIndexCount);

        // Process triangles from back to front to avoid overwriting data we still need
        for (int i = static_cast<int>(currentTriangleCount) - 1; i >= 0; --i)
        {
            const size_t oldOffset = i * 3;
            const size_t newOffset = i * 12;

            const VertexIndex v0 = indices[oldOffset];
            const VertexIndex v1 = indices[oldOffset + 1];
            const VertexIndex v2 = indices[oldOffset + 2];

            // Get or create midpoint vertices (with deduplication)
            const VertexIndex m01 = getMidpoint(v0, v1);
            const VertexIndex m12 = getMidpoint(v1, v2);
            const VertexIndex m20 = getMidpoint(v2, v0);

            // Create 4 new triangles directly in the output positions
            indices[newOffset + 0] = v0;   indices[newOffset + 1] = m01;  indices[newOffset + 2] = m20;
            indices[newOffset + 3] = v1;   indices[newOffset + 4] = m12;  indices[newOffset + 5] = m01;
            indices[newOffset + 6] = v2;   indices[newOffset + 7] = m20;  indices[newOffset + 8] = m12;
            indices[newOffset + 9] = m01;  indices[newOffset + 10] = m12; indices[newOffset + 11] = m20;
        }
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
    eassert(height > 0);
    eassert(diameter > 0);
    eassert(smoothness > 0);

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;
    const float radius = diameter * 0.5f;
    const float halfHeight = height * 0.5f;

    // Clamp smoothness and calculate segments
    const float s = std::max(1.0f, std::min(10.0f, smoothness));
    const uint32_t segments = static_cast<uint32_t>(8 + s * 4); // 12 to 48 segments

    // Reserve exact sizes
    const uint32_t totalVertices = segments * 4 + 2; // sides + cap rings + centers
    const uint32_t totalIndices = segments * 12; // 4 quads (2 tri each) for sides + 2 caps
    vertices.reserve(totalVertices);
    indices.reserve(totalIndices);

    // Side vertices (top and bottom rings with radial normals)
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const float theta = 2.0f * M_PI * static_cast<float>(seg) / static_cast<float>(segments);
        const float x = radius * std::cos(theta);
        const float z = radius * std::sin(theta);
        const VertexNormal normal = VertexNormal(x / radius, 0.0f, z / radius).Normalize();

        // Bottom vertex
        vertices.push_back({ { x, -halfHeight, z }, normal });
        // Top vertex
        vertices.push_back({ { x, halfHeight, z }, normal });
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

        // Second triangle (clockwise)
        indices.push_back(next);
        indices.push_back(current + 1);
        indices.push_back(next + 1);
    }

    // Cap vertices (separate from side vertices due to different normals)
    const uint32_t bottomCapStart = segments * 2;
    const uint32_t topCapStart = bottomCapStart + segments + 1;

    // Bottom cap center
    vertices.push_back({ { 0.0f, -halfHeight, 0.0f }, { 0.0f, -1.0f, 0.0f } });

    // Bottom cap ring
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const float theta = 2.0f * M_PI * static_cast<float>(seg) / static_cast<float>(segments);
        const float x = radius * std::cos(theta);
        const float z = radius * std::sin(theta);
        vertices.push_back({ { x, -halfHeight, z }, { 0.0f, -1.0f, 0.0f } });
    }

    // Top cap center
    vertices.push_back({ { 0.0f, halfHeight, 0.0f }, { 0.0f, 1.0f, 0.0f } });

    // Top cap ring
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const float theta = 2.0f * M_PI * static_cast<float>(seg) / static_cast<float>(segments);
        const float x = radius * std::cos(theta);
        const float z = radius * std::sin(theta);
        vertices.push_back({ { x, halfHeight, z }, { 0.0f, 1.0f, 0.0f } });
    }

    // Bottom cap indices (clockwise from below)
    const uint32_t bottomCenter = bottomCapStart;
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const uint32_t current = bottomCapStart + 1 + seg;
        const uint32_t next = bottomCapStart + 1 + ((seg + 1) % segments);

        indices.push_back(bottomCenter);
        indices.push_back(current);
        indices.push_back(next);
    }

    // Top cap indices (clockwise from above)
    const uint32_t topCenter = topCapStart;
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const uint32_t current = topCapStart + 1 + seg;
        const uint32_t next = topCapStart + 1 + ((seg + 1) % segments);

        indices.push_back(topCenter);
        indices.push_back(next);
        indices.push_back(current);
    }

    return Geometry{ std::move(vertices), std::move(indices) };
}

Shapes::Geometry
Shapes::Cone(const float diameter1, const float diameter2, const float smoothness)
{
    eassert(diameter1 >= 0);
    eassert(diameter2 >= 0);
    eassert(diameter1 > 0 || diameter2 > 0);
    eassert(smoothness > 0);

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    const float radius1 = diameter1 * 0.5f; // Bottom radius
    const float radius2 = diameter2 * 0.5f; // Top radius
    const float height = 1.0f;
    const float halfHeight = height * 0.5f;

    // Clamp smoothness and calculate segments
    const float s = std::max(1.0f, std::min(10.0f, smoothness));
    const uint32_t segments = static_cast<uint32_t>(8 + s * 4); // 12 to 48 segments

    // Calculate exact sizes
    const bool hasBottomCap = radius1 > 0.0f;
    const bool hasTopCap = radius2 > 0.0f;
    const bool hasSideQuads = radius1 > 0.0f && radius2 > 0.0f;

    uint32_t totalVertices = segments * 2; // Side vertices
    if (hasBottomCap) totalVertices += segments + 1; // Bottom cap ring + center
    if (hasTopCap) totalVertices += segments + 1;    // Top cap ring + center

    uint32_t totalIndices = segments * 3; // At least triangular side
    if (hasSideQuads) totalIndices += segments * 3; // Additional triangles for quads
    if (hasBottomCap) totalIndices += segments * 3;
    if (hasTopCap) totalIndices += segments * 3;

    vertices.reserve(totalVertices);
    indices.reserve(totalIndices);

    // Calculate slant normal for the cone's side
    const float dr = radius2 - radius1;
    const float slantLength = std::sqrt(dr * dr + height * height);
    const float normalY = dr / slantLength;
    const float normalXZ = height / slantLength;

    // Side vertices with slant normals
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const float theta = 2.0f * M_PI * static_cast<float>(seg) / static_cast<float>(segments);
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);

        const float x1 = radius1 * cosTheta;
        const float z1 = radius1 * sinTheta;
        const float x2 = radius2 * cosTheta;
        const float z2 = radius2 * sinTheta;

        const VertexNormal normal = VertexNormal{
            cosTheta * normalXZ,
            normalY,
            sinTheta * normalXZ
        }.Normalize();

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
        if (hasSideQuads)
        {
            indices.push_back(next);
            indices.push_back(current + 1);
            indices.push_back(next + 1);
        }
    }

    uint32_t currentVertexOffset = segments * 2;

    // Bottom cap (only if radius1 > 0)
    if (hasBottomCap)
    {
        const uint32_t bottomCenter = currentVertexOffset;
        const uint32_t bottomRingStart = currentVertexOffset + 1;

        // Bottom cap center
        vertices.push_back({ { 0.0f, -halfHeight, 0.0f }, { 0.0f, -1.0f, 0.0f } });

        // Bottom cap ring with vertical normals
        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            const float theta = 2.0f * M_PI * static_cast<float>(seg) / static_cast<float>(segments);
            const float x = radius1 * std::cos(theta);
            const float z = radius1 * std::sin(theta);
            vertices.push_back({ { x, -halfHeight, z }, { 0.0f, -1.0f, 0.0f } });
        }

        // Bottom cap indices
        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            const uint32_t current = bottomRingStart + seg;
            const uint32_t next = bottomRingStart + ((seg + 1) % segments);

            indices.push_back(bottomCenter);
            indices.push_back(current);
            indices.push_back(next);
        }

        currentVertexOffset += segments + 1;
    }

    // Top cap (only if radius2 > 0)
    if (hasTopCap)
    {
        const uint32_t topCenter = currentVertexOffset;
        const uint32_t topRingStart = currentVertexOffset + 1;

        // Top cap center
        vertices.push_back({ { 0.0f, halfHeight, 0.0f }, { 0.0f, 1.0f, 0.0f } });

        // Top cap ring with vertical normals
        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            const float theta = 2.0f * M_PI * static_cast<float>(seg) / static_cast<float>(segments);
            const float x = radius2 * std::cos(theta);
            const float z = radius2 * std::sin(theta);
            vertices.push_back({ { x, halfHeight, z }, { 0.0f, 1.0f, 0.0f } });
        }

        // Top cap indices
        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            const uint32_t current = topRingStart + seg;
            const uint32_t next = topRingStart + ((seg + 1) % segments);

            indices.push_back(topCenter);
            indices.push_back(next);
            indices.push_back(current);
        }
    }

    return Geometry{ std::move(vertices), std::move(indices) };
}

Shapes::Geometry
Shapes::Torus(
    const float ringDiameter,
    const float tubeDiameter,
    const float smoothness)
{
    eassert(ringDiameter >= 0);
    eassert(tubeDiameter > 0);
    eassert(smoothness > 0);

    if (0 == ringDiameter)
    {
        return Ball(tubeDiameter, smoothness);
    }

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    const float ringRadius = ringDiameter * 0.5f;
    const float tubeRadius = tubeDiameter * 0.5f;

    // Minimum smoothness of 3 to avoid degenerate geometry
    const uint32_t numSegmentsMajor = std::max(3u, static_cast<uint32_t>(smoothness * 4));
    const uint32_t numSegmentsMinor = std::max(3u, static_cast<uint32_t>(smoothness * 4));

    const float dTheta = 2.0f * M_PI / static_cast<float>(numSegmentsMajor);
    const float dPhi = 2.0f * M_PI / static_cast<float>(numSegmentsMinor);

    // Reserve exact memory
    const uint32_t totalVertices = numSegmentsMajor * numSegmentsMinor;
    const uint32_t totalIndices = numSegmentsMajor * numSegmentsMinor * 6;
    vertices.reserve(totalVertices);
    indices.reserve(totalIndices);

    // Precompute trig values for major circle
    std::vector<float> cosThetaCache(numSegmentsMajor);
    std::vector<float> sinThetaCache(numSegmentsMajor);
    for (uint32_t i = 0; i < numSegmentsMajor; ++i)
    {
        const float theta = i * dTheta;
        cosThetaCache[i] = std::cos(theta);
        sinThetaCache[i] = std::sin(theta);
    }

    // Precompute trig values for minor circle
    std::vector<float> cosPhiCache(numSegmentsMinor);
    std::vector<float> sinPhiCache(numSegmentsMinor);
    for (uint32_t j = 0; j < numSegmentsMinor; ++j)
    {
        const float phi = j * dPhi;
        cosPhiCache[j] = std::cos(phi);
        sinPhiCache[j] = std::sin(phi);
    }

    // Generate vertices
    for (uint32_t i = 0; i < numSegmentsMajor; ++i)
    {
        const float cosTheta = cosThetaCache[i];
        const float sinTheta = sinThetaCache[i];

        for (uint32_t j = 0; j < numSegmentsMinor; ++j)
        {
            const float cosPhi = cosPhiCache[j];
            const float sinPhi = sinPhiCache[j];

            // Vertex position (left-handed)
            const float distanceFromCenter = ringRadius + tubeRadius * cosPhi;
            const float x = distanceFromCenter * cosTheta;
            const float y = distanceFromCenter * sinTheta;
            const float z = tubeRadius * sinPhi;

            // Unit normal (same calculation but clearer with explicit names)
            const float nx = cosTheta * cosPhi;
            const float ny = sinTheta * cosPhi;
            const float nz = sinPhi;

            vertices.push_back(Vertex{
                { x, y, z },
                { nx, ny, nz }
                });
        }
    }

    // Generate triangle indices (clockwise for left-handed system)
    for (uint32_t i = 0; i < numSegmentsMajor; ++i)
    {
        const uint32_t nextI = (i + 1) % numSegmentsMajor;
        const uint32_t rowOffset = i * numSegmentsMinor;
        const uint32_t nextRowOffset = nextI * numSegmentsMinor;

        for (uint32_t j = 0; j < numSegmentsMinor; ++j)
        {
            const uint32_t nextJ = (j + 1) % numSegmentsMinor;

            const uint32_t i0 = rowOffset + j;
            const uint32_t i1 = nextRowOffset + j;
            const uint32_t i2 = nextRowOffset + nextJ;
            const uint32_t i3 = rowOffset + nextJ;

            // First triangle
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            // Second triangle
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }

    return Geometry{ std::move(vertices), std::move(indices) };
}