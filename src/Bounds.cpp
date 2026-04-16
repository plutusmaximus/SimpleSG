#include "Bounds.h"

AABoundingBox
AABoundingBox::FromVertices(std::span<const Vertex> vertices, std::span<const VertexIndex> indices)
{
    AABoundingBox result;

    if(vertices.empty())
    {
        return result;
    }

    Vec3f min = vertices[indices[0]].pos;
    Vec3f max = vertices[indices[0]].pos;

    for(const VertexIndex& index : indices)
    {
        const Vec3f& p = vertices[index].pos;
        min.x = std::min(min.x, p.x);
        min.y = std::min(min.y, p.y);
        min.z = std::min(min.z, p.z);

        max.x = std::max(max.x, p.x);
        max.y = std::max(max.y, p.y);
        max.z = std::max(max.z, p.z);
    }

    result.m_Min = min;
    result.m_Max = max;
    return result;
}