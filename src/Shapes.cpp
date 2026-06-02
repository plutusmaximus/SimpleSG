#include "Shapes.h"
#include "AssertHelper.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>

namespace
{
constexpr float kPi = std::numbers::pi_v<float>;
}

Shapes::Geometry
Shapes::Box(const float width, const float height, const float depth)
{
    MLG_ASSERT(width > 0);
    MLG_ASSERT(height > 0);
    MLG_ASSERT(depth > 0);

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    const float hw = width * 0.5f;
    const float hh = height * 0.5f;
    const float hd = depth * 0.5f;

    // 8 vertices - one per corner
    vertices.reserve(8);
    indices.reserve(36);

    // Calculate normalized normals for each corner (average of 3 adjacent faces)
    const float invSqrt3 = std::numbers::inv_sqrt3_v<float>;

    // Vertex order:
    // 0: (-x, -y, -z)  1: (+x, -y, -z)
    // 2: (+x, +y, -z)  3: (-x, +y, -z)
    // 4: (-x, -y, +z)  5: (+x, -y, +z)
    // 6: (+x, +y, +z)  7: (-x, +y, +z)

    vertices.emplace_back(Vertex{ .pos{ -hw, -hh, -hd }, .normal{ -invSqrt3, -invSqrt3, -invSqrt3 }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{  hw, -hh, -hd }, .normal{  invSqrt3, -invSqrt3, -invSqrt3 }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{  hw,  hh, -hd }, .normal{  invSqrt3,  invSqrt3, -invSqrt3 }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{ -hw,  hh, -hd }, .normal{ -invSqrt3,  invSqrt3, -invSqrt3 }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{ -hw, -hh,  hd }, .normal{ -invSqrt3, -invSqrt3,  invSqrt3 }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{  hw, -hh,  hd }, .normal{  invSqrt3, -invSqrt3,  invSqrt3 }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{  hw,  hh,  hd }, .normal{  invSqrt3,  invSqrt3,  invSqrt3 }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{ -hw,  hh,  hd }, .normal{ -invSqrt3,  invSqrt3,  invSqrt3 }, .uvs{} });

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
Shapes::Ball(const float radius, const float smoothness)
{
    MLG_ASSERT(radius > 0);
    MLG_ASSERT(smoothness > 0);

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    // Clamp smoothness to determine subdivision level
    const float s = std::max(1.0f, std::min(10.0f, smoothness));
    const size_t subdivisions = static_cast<size_t>(s * 0.3f); // 0 to 3 subdivisions

    // Calculate exact final sizes with deduplication
    // With deduplication, vertices are shared between triangles
    // Formula: V = 10 * 4^n + 2 (for n subdivisions)
    const size_t finalTriangles = 20 * (1uz << (2 * subdivisions)); // 20 * 4^subdivisions
    const size_t finalIndices = finalTriangles * 3;
    const size_t totalVertices = subdivisions > 0 ? (10 * (1uz << (2 * subdivisions))) + 2 : 12;

    vertices.reserve(totalVertices);
    indices.reserve(finalIndices);

    // Create icosahedron base vertices
    const float t = std::numbers::phi_v<float>; // Golden ratio
    const float len = std::sqrt(1.0f + (t * t));
    const float a = 1.0f / len;
    const float b = t / len;

    // 12 vertices of icosahedron
    vertices.emplace_back(Vertex{ .pos{ -a,  b,  0 }, .normal{ -a,  b,  0 }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{  a,  b,  0 }, .normal{  a,  b,  0 }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{ -a, -b,  0 }, .normal{ -a, -b,  0 }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{  a, -b,  0 }, .normal{  a, -b,  0 }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{  0, -a,  b }, .normal{  0, -a,  b }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{  0,  a,  b }, .normal{  0,  a,  b }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{  0, -a, -b }, .normal{  0, -a, -b }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{  0,  a, -b }, .normal{  0,  a, -b }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{  b,  0, -a }, .normal{  b,  0, -a }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{  b,  0,  a }, .normal{  b,  0,  a }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{ -b,  0, -a }, .normal{ -b,  0, -a }, .uvs{} });
    vertices.emplace_back(Vertex{ .pos{ -b,  0,  a }, .normal{ -b,  0,  a }, .uvs{} });

    // 20 faces of icosahedron (clockwise winding)
    const VertexIndex faces[][3] =
    {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1}
    };

    for (const auto& face : faces)
    {
        indices.push_back(face[0]);
        indices.push_back(face[1]);
        indices.push_back(face[2]);
    }

    // Hoist midpoint cache outside the loop
    std::unordered_map<uint64_t, VertexIndex> midpointCache;

    // Lambda to get or create midpoint vertex
    auto getMidpoint = [&](VertexIndex v0, VertexIndex v1) -> VertexIndex {
        // Ensure consistent ordering for the key
        if (v0 > v1) {std::swap(v0, v1);}
        const uint64_t key = (static_cast<uint64_t>(v0) << 32) | v1;

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
        mid = mid.Normalize();

        const VertexIndex newIdx = static_cast<VertexIndex>(vertices.size());
        vertices.emplace_back(Vertex{ .pos = mid, .normal{mid.x, mid.y, mid.z}, .uvs{} });
        midpointCache[key] = newIdx;
        return newIdx;
    };

    // Subdivide triangles with vertex deduplication
    for (size_t subdiv = 0; subdiv < subdivisions; ++subdiv)
    {
        midpointCache.clear(); // Clear cache for each subdivision level

        const size_t currentTriangleCount = indices.size() / 3;
        const size_t newIndexCount = currentTriangleCount * 12; // Each triangle becomes 4 triangles (12 indices)

        // Resize to final size for this iteration
        indices.resize(newIndexCount);

        // Process triangles from back to front to avoid overwriting data we still need
        for (int i = static_cast<int>(currentTriangleCount) - 1; i >= 0; --i)
        {
            const size_t oldOffset = static_cast<size_t>(i) * 3;
            const size_t newOffset = static_cast<size_t>(i) * 12;

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
    for (auto& vertex : vertices)
    {
        vertex.pos.x *= radius;
        vertex.pos.y *= radius;
        vertex.pos.z *= radius;
    }

    return Geometry{ std::move(vertices), std::move(indices) };
}

Shapes::Geometry
Shapes::Cylinder(const float height, const float radius, const float smoothness)
{
    MLG_ASSERT(height > 0);
    MLG_ASSERT(radius > 0);
    MLG_ASSERT(smoothness > 0);

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;
    const float halfHeight = height * 0.5f;

    // Clamp smoothness and calculate segments
    const float s = std::max(1.0f, std::min(10.0f, smoothness));
    const uint32_t segments = static_cast<uint32_t>(8 + (s * 4)); // 12 to 48 segments

    // Reserve exact sizes
    const uint32_t totalVertices = (segments * 4) + 2; // sides + cap rings + centers
    const uint32_t totalIndices = segments * 12; // 4 quads (2 tri each) for sides + 2 caps
    vertices.reserve(totalVertices);
    indices.reserve(totalIndices);

    // Side vertices (top and bottom rings with radial normals)
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const float theta = 2.0f * kPi * static_cast<float>(seg) / static_cast<float>(segments);
        const float x = radius * std::cos(theta);
        const float z = radius * std::sin(theta);
        const VertexNormal normal = VertexNormal(x / radius, 0.0f, z / radius).Normalize();

        // Bottom vertex
        vertices.emplace_back(Vertex{ .pos{ x, -halfHeight, z }, .normal = normal, .uvs{} });
        // Top vertex
        vertices.emplace_back(Vertex{ .pos{ x, halfHeight, z }, .normal = normal, .uvs{} });
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
    vertices.emplace_back(Vertex{ .pos{ 0.0f, -halfHeight, 0.0f }, .normal{ 0.0f, -1.0f, 0.0f }, .uvs{} });

    // Bottom cap ring
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const float theta = 2.0f * kPi * static_cast<float>(seg) / static_cast<float>(segments);
        const float x = radius * std::cos(theta);
        const float z = radius * std::sin(theta);
        vertices.emplace_back(Vertex{ .pos{ x, -halfHeight, z }, .normal{ 0.0f, -1.0f, 0.0f }, .uvs{} });
    }

    // Top cap center
    vertices.emplace_back(Vertex{ .pos{ 0.0f, halfHeight, 0.0f }, .normal{ 0.0f, 1.0f, 0.0f }, .uvs{} });

    // Top cap ring
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const float theta = 2.0f * kPi * static_cast<float>(seg) / static_cast<float>(segments);
        const float x = radius * std::cos(theta);
        const float z = radius * std::sin(theta);
        vertices.emplace_back(Vertex{ .pos{ x, halfHeight, z }, .normal{ 0.0f, 1.0f, 0.0f }, .uvs{} });
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
Shapes::Cone(const float radius1, const float radius2, const float smoothness)
{
    MLG_ASSERT(radius1 >= 0);
    MLG_ASSERT(radius2 >= 0);
    MLG_ASSERT(radius1 > 0 || radius2 > 0);
    MLG_ASSERT(smoothness > 0);

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    const float height = 1.0f;
    const float halfHeight = height * 0.5f;

    // Clamp smoothness and calculate segments
    const float s = std::max(1.0f, std::min(10.0f, smoothness));
    const uint32_t segments = static_cast<uint32_t>(8 + (s * 4)); // 12 to 48 segments

    // Calculate exact sizes
    const bool hasBottomCap = radius1 > 0.0f;
    const bool hasTopCap = radius2 > 0.0f;
    const bool hasSideQuads = radius1 > 0.0f && radius2 > 0.0f;

    uint32_t totalVertices = segments * 2; // Side vertices
    if (hasBottomCap) {totalVertices += segments + 1; } // Bottom cap ring + center
    if (hasTopCap) {totalVertices += segments + 1; }    // Top cap ring + center

    uint32_t totalIndices = segments * 3; // At least triangular side
    if (hasSideQuads) {totalIndices += segments * 3; } // Additional triangles for quads
    if (hasBottomCap) {totalIndices += segments * 3; }
    if (hasTopCap) {totalIndices += segments * 3; }

    vertices.reserve(totalVertices);
    indices.reserve(totalIndices);

    // Calculate slant normal for the cone's side
    const float dr = radius2 - radius1;
    const float slantLength = std::sqrt((dr * dr) + (height * height));
    const float normalY = dr / slantLength;
    const float normalXZ = height / slantLength;

    // Side vertices with slant normals
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        const float theta = 2.0f * kPi * static_cast<float>(seg) / static_cast<float>(segments);
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
        vertices.emplace_back(Vertex{ .pos{ x1, -halfHeight, z1 }, .normal = normal, .uvs{} });
        // Top vertex
        vertices.emplace_back(Vertex{ .pos{ x2, halfHeight, z2 }, .normal = normal, .uvs{} });
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
        vertices.emplace_back(Vertex{ .pos{ 0.0f, -halfHeight, 0.0f }, .normal{ 0.0f, -1.0f, 0.0f }, .uvs{} });

        // Bottom cap ring with vertical normals
        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            const float theta = 2.0f * kPi * static_cast<float>(seg) / static_cast<float>(segments);
            const float x = radius1 * std::cos(theta);
            const float z = radius1 * std::sin(theta);
            vertices.emplace_back(Vertex{ .pos{ x, -halfHeight, z }, .normal{ 0.0f, -1.0f, 0.0f }, .uvs{} });
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
        vertices.emplace_back(Vertex{ .pos{ 0.0f, halfHeight, 0.0f }, .normal{ 0.0f, 1.0f, 0.0f }, .uvs{} });

        // Top cap ring with vertical normals
        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            const float theta = 2.0f * kPi * static_cast<float>(seg) / static_cast<float>(segments);
            const float x = radius2 * std::cos(theta);
            const float z = radius2 * std::sin(theta);
            vertices.emplace_back(Vertex{ .pos{ x, halfHeight, z }, .normal{ 0.0f, 1.0f, 0.0f }, .uvs{} });
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
Shapes::Torus(const float ringRadius, const float tubeRadius, const float smoothness)
{
    MLG_ASSERT(ringRadius >= 0);
    MLG_ASSERT(tubeRadius > 0);
    MLG_ASSERT(smoothness > 0);

    if (0 == ringRadius)
    {
        return Ball(tubeRadius, smoothness);
    }

    std::vector<Vertex> vertices;
    std::vector<VertexIndex> indices;

    // Minimum smoothness of 3 to avoid degenerate geometry
    const size_t numSegmentsMajor = std::max(3uz, static_cast<size_t>(smoothness * 4));
    const size_t numSegmentsMinor = std::max(3uz, static_cast<size_t>(smoothness * 4));

    const float dTheta = 2.0f * kPi / static_cast<float>(numSegmentsMajor);
    const float dPhi = 2.0f * kPi / static_cast<float>(numSegmentsMinor);

    // Reserve exact memory
    const size_t totalVertices = numSegmentsMajor * numSegmentsMinor;
    const size_t totalIndices = numSegmentsMajor * numSegmentsMinor * 6;
    vertices.reserve(totalVertices);
    indices.reserve(totalIndices);

    // Precompute trig values for major circle
    std::vector<float> cosThetaCache(numSegmentsMajor);
    std::vector<float> sinThetaCache(numSegmentsMajor);
    for (size_t i = 0; i < numSegmentsMajor; ++i)
    {
        const float theta = static_cast<float>(i) * dTheta;
        cosThetaCache[i] = std::cos(theta);
        sinThetaCache[i] = std::sin(theta);
    }

    // Precompute trig values for minor circle
    std::vector<float> cosPhiCache(numSegmentsMinor);
    std::vector<float> sinPhiCache(numSegmentsMinor);
    for (size_t j = 0; j < numSegmentsMinor; ++j)
    {
        const float phi = static_cast<float>(j) * dPhi;
        cosPhiCache[j] = std::cos(phi);
        sinPhiCache[j] = std::sin(phi);
    }

    // Generate vertices
    for (size_t i = 0; i < numSegmentsMajor; ++i)
    {
        const float cosTheta = cosThetaCache[i];
        const float sinTheta = sinThetaCache[i];

        for (size_t j = 0; j < numSegmentsMinor; ++j)
        {
            const float cosPhi = cosPhiCache[j];
            const float sinPhi = sinPhiCache[j];

            // Vertex position (left-handed)
            const float distanceFromCenter = ringRadius + (tubeRadius * cosPhi);
            const float x = distanceFromCenter * cosTheta;
            const float y = distanceFromCenter * sinTheta;
            const float z = tubeRadius * sinPhi;

            // Unit normal (same calculation but clearer with explicit names)
            const float nx = cosTheta * cosPhi;
            const float ny = sinTheta * cosPhi;
            const float nz = sinPhi;

            vertices.emplace_back(Vertex{ .pos{ x, y, z }, .normal{ nx, ny, nz }, .uvs{} });
        }
    }

    // Generate triangle indices (clockwise for left-handed system)
    for (size_t i = 0; i < numSegmentsMajor; ++i)
    {
        const size_t nextI = (i + 1) % numSegmentsMajor;
        const size_t rowOffset = i * numSegmentsMinor;
        const size_t nextRowOffset = nextI * numSegmentsMinor;

        for (size_t j = 0; j < numSegmentsMinor; ++j)
        {
            const size_t nextJ = (j + 1) % numSegmentsMinor;

            const size_t i0 = rowOffset + j;
            const size_t i1 = nextRowOffset + j;
            const size_t i2 = nextRowOffset + nextJ;
            const size_t i3 = rowOffset + nextJ;

            // First triangle
            indices.push_back(static_cast<VertexIndex>(i0));
            indices.push_back(static_cast<VertexIndex>(i1));
            indices.push_back(static_cast<VertexIndex>(i2));

            // Second triangle
            indices.push_back(static_cast<VertexIndex>(i0));
            indices.push_back(static_cast<VertexIndex>(i2));
            indices.push_back(static_cast<VertexIndex>(i3));
        }
    }

    return Geometry{ std::move(vertices), std::move(indices) };
}