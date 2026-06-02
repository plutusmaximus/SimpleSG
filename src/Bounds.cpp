#include "Bounds.h"

Box Box::FromVertices(std::span<const Vertex> vertices, std::span<const VertexIndex> indices)
{
    Box result;

    if (vertices.empty())
    {
        return result;
    }

    Vec3f min = vertices[indices[0]].pos;
    Vec3f max = vertices[indices[0]].pos;

    for (const VertexIndex& index : indices)
    {
        const Vec3f& pos = vertices[index].pos;
        min.x = std::min(min.x, pos.x);
        min.y = std::min(min.y, pos.y);
        min.z = std::min(min.z, pos.z);

        max.x = std::max(max.x, pos.x);
        max.y = std::max(max.y, pos.y);
        max.z = std::max(max.z, pos.z);
    }

    result.m_HalfExtents = (max - min) * 0.5f;
    return result;
}