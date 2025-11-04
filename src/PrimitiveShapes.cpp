#include "PrimitiveShapes.h"
#include <cmath>
#include <algorithm>
#include <numbers>

static constexpr float M_PI = std::numbers::pi_v<float>;

// Helper function to normalize a vector
static void Normalize(VertexNormal& n)
{
    float length = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
    if (length > 0.0f)
    {
        n.x /= length;
        n.y /= length;
        n.z /= length;
    }
}

void MakeBox(const float width, const float height, const float depth,
    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
    vertices.clear();
    indices.clear();

    float hw = width * 0.5f;
    float hh = height * 0.5f;
    float hd = depth * 0.5f;

    // 8 vertices - one per corner
    vertices.reserve(8);
    indices.reserve(36);

    // Calculate normalized normals for each corner (average of 3 adjacent faces)
    float invSqrt3 = 1.0f / std::sqrt(3.0f);

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
}

void MakeBall(const float diameter, const float smoothness,
    std::vector<Vertex>& vertices, std::vector<VertexIndex>& indices)
{
    vertices.clear();
    indices.clear();

    float radius = diameter * 0.5f;

    // Clamp smoothness and calculate segments
    float s = std::max(1.0f, std::min(10.0f, smoothness));
    uint32_t segments = static_cast<uint32_t>(8 + s * 4); // 12 to 48 segments
    uint32_t rings = segments / 2;

    vertices.reserve((rings + 1) * (segments + 1));
    indices.reserve(rings * segments * 6);

    // Generate vertices
    for (uint32_t ring = 0; ring <= rings; ++ring)
    {
        float phi = M_PI * static_cast<float>(ring) / static_cast<float>(rings);
        float y = radius * std::cos(phi);
        float ringRadius = radius * std::sin(phi);

        for (uint32_t seg = 0; seg <= segments; ++seg)
        {
            float theta = 2.0f * M_PI * static_cast<float>(seg) / static_cast<float>(segments);
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);

            Vertex v;
            v.pos = { x, y, z };
            v.normal = { x / radius, y / radius, z / radius };
            Normalize(v.normal);

            vertices.push_back(v);
        }
    }

    // Generate indices (clockwise winding for left-handed system)
    for (uint32_t ring = 0; ring < rings; ++ring)
    {
        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            VertexIndex current = ring * (segments + 1) + seg;
            VertexIndex next = current + segments + 1;

            // First triangle (clockwise)
            indices.push_back(current);
            indices.push_back(current + 1);
            indices.push_back(next);

            // Second triangle (clockwise)
            indices.push_back(next);
            indices.push_back(current + 1);
            indices.push_back(next + 1);
        }
    }
}

void MakeCylinder(const float height, const float diameter, const float smoothness,
    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
    vertices.clear();
    indices.clear();

    float radius = diameter * 0.5f;
    float halfHeight = height * 0.5f;

    // Clamp smoothness and calculate segments
    float s = std::max(1.0f, std::min(10.0f, smoothness));
    uint32_t segments = static_cast<uint32_t>(8 + s * 4); // 12 to 48 segments

    vertices.reserve(segments * 2 + 2);
    indices.reserve(segments * 12);

    // Side vertices (top and bottom rings)
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        float theta = 2.0f * M_PI * static_cast<float>(seg) / static_cast<float>(segments);
        float x = radius * std::cos(theta);
        float z = radius * std::sin(theta);

        VertexNormal normal = { x / radius, 0.0f, z / radius };
        Normalize(normal);

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
        indices.push_back(next);
        indices.push_back(current + 1);

        // Second triangle (clockwise)
        indices.push_back(current + 1);
        indices.push_back(next);
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
        indices.push_back(next);
        indices.push_back(current);
    }

    // Top cap indices (clockwise from above, using side vertices)
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        uint32_t current = seg * 2 + 1;
        uint32_t next = ((seg + 1) % segments) * 2 + 1;

        indices.push_back(topCenter);
        indices.push_back(current);
        indices.push_back(next);
    }
}

void MakeCone(const float diameter1, const float diameter2, const float smoothness,
    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
    vertices.clear();
    indices.clear();

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

        VertexNormal normal = { cosTheta * normalXZ, normalY, sinTheta * normalXZ };
        Normalize(normal);

        // Bottom vertex
        vertices.push_back({ { x1, -halfHeight, z1 }, normal });

        // Top vertex
        vertices.push_back({ { x2, halfHeight, z2 }, normal });
    }

    // Generate side indices
    for (uint32_t seg = 0; seg < segments; ++seg)
    {
        uint32_t current = seg * 2;
        uint32_t next = ((seg + 1) % segments) * 2;

        // First triangle (clockwise)
        indices.push_back(current);
        indices.push_back(next);
        indices.push_back(current + 1);

        // Second triangle (clockwise) - only if both radii are non-zero
        if (radius1 > 0.0f && radius2 > 0.0f)
        {
            indices.push_back(current + 1);
            indices.push_back(next);
            indices.push_back(next + 1);
        }
    }

    uint32_t bottomCenter = segments * 2;
    uint32_t topCenter = bottomCenter + 1;

    // Bottom cap (only if radius1 > 0)
    if (radius1 > 0.0f)
    {
        vertices.push_back({ { 0.0f, -halfHeight, 0.0f }, { 0.0f, -1.0f, 0.0f } });

        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            uint32_t current = seg * 2;
            uint32_t next = ((seg + 1) % segments) * 2;

            indices.push_back(bottomCenter);
            indices.push_back(next);
            indices.push_back(current);
        }
    }

    // Top cap (only if radius2 > 0)
    if (radius2 > 0.0f)
    {
        vertices.push_back({ { 0.0f, halfHeight, 0.0f }, { 0.0f, 1.0f, 0.0f } });

        for (uint32_t seg = 0; seg < segments; ++seg)
        {
            uint32_t current = seg * 2 + 1;
            uint32_t next = ((seg + 1) % segments) * 2 + 1;

            indices.push_back(topCenter);
            indices.push_back(current);
            indices.push_back(next);
        }
    }
}

void MakeTorus(const float majorDiameter, const float minorDiameter, const float smoothness,
    std::vector<Vertex>& vertices, std::vector<uint32_t>& indices)
{
    vertices.clear();
    indices.clear();

    float majorRadius = majorDiameter * 0.5f; // Distance from origin to tube center
    float minorRadius = minorDiameter * 0.5f; // Tube radius

    // Clamp smoothness and calculate segments
    float s = std::max(1.0f, std::min(10.0f, smoothness));
    uint32_t majorSegments = static_cast<uint32_t>(8 + s * 4); // Around the ring
    uint32_t minorSegments = static_cast<uint32_t>(6 + s * 2); // Around the tube

    vertices.reserve(majorSegments * minorSegments);
    indices.reserve(majorSegments * minorSegments * 6);

    // Generate vertices
    for (uint32_t i = 0; i < majorSegments; ++i)
    {
        float u = 2.0f * M_PI * static_cast<float>(i) / static_cast<float>(majorSegments);
        float cosU = std::cos(u);
        float sinU = std::sin(u);

        for (uint32_t j = 0; j < minorSegments; ++j)
        {
            float v = 2.0f * M_PI * static_cast<float>(j) / static_cast<float>(minorSegments);
            float cosV = std::cos(v);
            float sinV = std::sin(v);

            // Position on the torus
            float x = (majorRadius + minorRadius * cosV) * cosU;
            float y = minorRadius * sinV;
            float z = (majorRadius + minorRadius * cosV) * sinU;

            // Normal calculation
            // The normal at any point on the torus points from the tube center
            float tubeX = majorRadius * cosU;
            float tubeZ = majorRadius * sinU;

            VertexNormal normal = { x - tubeX, y, z - tubeZ };
            Normalize(normal);

            vertices.push_back({ { x, y, z }, normal });
        }
    }

    // Generate indices (clockwise winding)
    for (uint32_t i = 0; i < majorSegments; ++i)
    {
        uint32_t nextI = (i + 1) % majorSegments;

        for (uint32_t j = 0; j < minorSegments; ++j)
        {
            uint32_t nextJ = (j + 1) % minorSegments;

            uint32_t i0 = i * minorSegments + j;
            uint32_t i1 = nextI * minorSegments + j;
            uint32_t i2 = nextI * minorSegments + nextJ;
            uint32_t i3 = i * minorSegments + nextJ;

            // First triangle (clockwise)
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            // Second triangle (clockwise)
            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i3);
        }
    }
}